#include "HaikuScale.h"
#include "HaikuCompositor.h"
#include "HaikuSubcompositor.h"
#include "HaikuShm.h"
#include "HaikuXdgSurface.h"
#include "HaikuXdgToplevel.h"
#include "HaikuXdgPopup.h"
#include "HaikuSeat.h"
#include "HaikuDataDeviceManager.h"
#include "FractionalTrace.h"
#include "Wayland.h"
#include "WaylandEnv.h"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <stdio.h>
#include <bit>

#include "AppKitPtrs.h"
#include <Application.h>
#include <View.h>
#include <Window.h>
#include <Bitmap.h>
#include <Region.h>
#include <Cursor.h>
#include <Entry.h>

extern const struct wl_interface wl_compositor_interface;


#define COMPOSITOR_VERSION 4

static void Assert(bool cond) {if (!cond) abort();}




class HaikuRegion: public WlRegion {
private:
	BRegion fRegion;

public:
	virtual ~HaikuRegion() = default;
	static HaikuRegion *FromResource(struct wl_resource *resource) {return (HaikuRegion*)WlResource::FromResource(resource);}

	const BRegion &Region() {return fRegion;}

	void HandleAdd(int32_t x, int32_t y, int32_t width, int32_t height) final;
	void HandleSubtract(int32_t x, int32_t y, int32_t width, int32_t height) final;
};

void HaikuRegion::HandleAdd(int32_t x, int32_t y, int32_t width, int32_t height)
{
	width = std::min(width, 1 << 24);
	height = std::min(height, 1 << 24);
	fRegion.Include(BRect(x, y, x + width - 1, y + height - 1));
}

void HaikuRegion::HandleSubtract(int32_t x, int32_t y, int32_t width, int32_t height)
{
	width = std::min(width, 1 << 24);
	height = std::min(height, 1 << 24);
	fRegion.Exclude(BRect(x, y, x + width - 1, y + height - 1));
}




class HaikuCompositor: public WlCompositor {
protected:
	virtual ~HaikuCompositor() = default;

public:
	void HandleCreateSurface(uint32_t id) override;
	void HandleCreateRegion(uint32_t id) override;
};


HaikuCompositorGlobal *HaikuCompositorGlobal::Create(struct wl_display *display)
{
	ObjectDeleter<HaikuCompositorGlobal> global(new(std::nothrow) HaikuCompositorGlobal());
	if (!global.IsSet()) return NULL;
	if (!global->Init(display, &wl_compositor_interface, COMPOSITOR_VERSION)) return NULL;
	return global.Detach();
}

void HaikuCompositorGlobal::Bind(struct wl_client *wl_client, uint32_t version, uint32_t id)
{
	HaikuCompositor *manager = new(std::nothrow) HaikuCompositor();
	if (manager == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	if (!manager->Init(wl_client, version, id)) {
		return;
	}
}


void HaikuCompositor::HandleCreateSurface(uint32_t id)
{
	HaikuSurface *surface = HaikuSurface::Create(Client(), Version(), id);
}

void HaikuCompositor::HandleCreateRegion(uint32_t id)
{
	HaikuRegion *region = new(std::nothrow) HaikuRegion();
	if (region == NULL) {
		wl_client_post_no_memory(Client());
		return;
	}
	if (!region->Init(Client(), Version(), id)) {
		return;
	}
}




class WaylandView: public BView {
private:
	friend class HaikuSurface;

	HaikuSurface *fSurface;
	uint32 fOldMouseBtns = 0;
	bool fFramerateLimitDisabled = false;
	WaylandEnv *fActiveWlEnv {};
	BBitmap *fOffscreenBitmap {};
	BView *fOffscreenView {};

	void DrawSurfaceTree(BView *targetView, HaikuSurface *surface, BPoint origin);
	BPoint PopupOriginInParent(HaikuXdgPopup *popup);
	void DrawPopupBackground(BView *targetView, HaikuXdgPopup *popup, BPoint origin);

public:
	WaylandView(HaikuSurface *surface);
	virtual ~WaylandView();

	HaikuSurface *Surface() {return fSurface;}

	void RemoveDescendantsAndSelf();

	void WindowActivated(bool active) final;
	void MessageReceived(BMessage *msg) final;
	void Draw(BRect dirty);
	void Pulse(void);
};


WaylandView::WaylandView(HaikuSurface *surface):
	BView(BRect(), "WaylandView", B_FOLLOW_NONE, B_WILL_DRAW | B_DRAW_ON_CHILDREN | B_INPUT_METHOD_AWARE | B_PULSE_NEEDED),
	fSurface(surface)
{
	SetViewColor(B_TRANSPARENT_COLOR);


	SetEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);

	char *envValue = getenv("HAIWAY_FRAMERATE_LIMIT");
	if (envValue != NULL) {
		fFramerateLimitDisabled = (strcmp(envValue, "disabled") == 0);
		if (fFramerateLimitDisabled) {
			SetFlags(Flags() & ~B_PULSE_NEEDED);
			fprintf(stderr, "Frame rate limiting disabled\n");
		}
	}
}

WaylandView::~WaylandView()
{
	delete fOffscreenBitmap;

	if (fSurface != NULL) {
		debugger("[!] ~WaylandView: bad deletion");
	}
}

void WaylandView::RemoveDescendantsAndSelf()
{
	while (BView* child = ChildAt(0)) {
		WaylandView* view = dynamic_cast<WaylandView*>(child);
		view->RemoveDescendantsAndSelf();
	}

	if (fActiveWlEnv != NULL)
		WaylandEnv::Wait(&fActiveWlEnv, Looper());
	RemoveSelf();
}

void WaylandView::WindowActivated(bool active)
{
	WaylandEnv wlEnv(this, &fActiveWlEnv);
	HaikuSeatGlobal *seat = HaikuGetSeat(fSurface->Client());
	if (seat == NULL) return;

	if (fSurface->Subsurface() != NULL) {
		return;
	}

	seat->SetKeyboardFocus(fSurface, active);
}

void WaylandView::MessageReceived(BMessage *msg)
{
	{
		WaylandEnv wlEnv(this, &fActiveWlEnv);

		HaikuSeatGlobal *seat = HaikuGetSeat(fSurface->Client());
		if (seat != NULL) {
			if (msg->what == B_COPY_TARGET) {
				entry_ref dirRef;
				BString name;
				if (msg->FindRef("directory", &dirRef) == B_OK && msg->FindString("name", &name) == B_OK) {
					if (seat->DataDevice() != NULL) {
						seat->DataDevice()->HandleCopyTargetReply(msg, dirRef, name);
						return;
					}
				}
			}

			bool isPointerMessage = true;
			BPoint where;
			if (msg->WasDropped()) {
				where = msg->DropPoint();
				AppKitPtrs::LockedPtr(this)->ConvertFromScreen(&where);
			} else if (msg->FindPoint("be:view_where", &where) < B_OK) {
				isPointerMessage = false;
			}
			HaikuSurface *surface = fSurface;


			while (isPointerMessage && !surface->InputRgnContains(ToLogical(where))
				&& surface->Subsurface() != NULL) {
				where += AppKitPtrs::LockedPtr(surface->View())->Frame().LeftTop();
				surface = surface->Subsurface()->Parent();
			}
			BMessage routed(*msg);
			if (isPointerMessage && !msg->WasDropped())
				routed.ReplacePoint("be:view_where", where);
			if (seat->MessageReceived(surface, &routed)) {
				return;
			}
		}
	}
	BView::MessageReceived(msg);
}

void WaylandView::DrawSurfaceTree(BView *targetView, HaikuSurface *surface, BPoint origin)
{
	if (surface == NULL) return;

	BBitmap *bmp = surface->Bitmap();
	if (bmp != NULL) {
		BRect viewRect = NativeRect(BRect(origin.x, origin.y, origin.x + surface->Size().width, origin.y + surface->Size().height));

		BRegion targetClip;
		targetView->GetClippingRegion(&targetClip);
		if (targetClip.Intersects(viewRect)) {
			drawing_mode mode = B_OP_COPY;
			switch (bmp->ColorSpace()) {
				case B_RGBA64:
				case B_RGBA32:
				case B_RGBA15:
				case B_RGBA64_BIG:
				case B_RGBA32_BIG:
				case B_RGBA15_BIG:
					mode = B_OP_ALPHA;
					break;
				default:
					mode = B_OP_COPY;
					break;
			}

			const auto &viewportSrc = surface->fState.viewportSrc;
			BRect bitmapRect = bmp->Bounds();
			if (viewportSrc.IsValid()) {
				float density = surface->fState.scale;
				bitmapRect = BRect(viewportSrc.x * density, viewportSrc.y * density,
					(viewportSrc.x + viewportSrc.width) * density - 1,
					(viewportSrc.y + viewportSrc.height) * density - 1);
			}

			if (surface->EnsureNativeCache(bmp, bitmapRect, viewRect, mode)) {
				targetView->SetDrawingMode(mode);
				targetView->DrawBitmap(surface->fNativeCache,
					surface->fNativeCache->Bounds(), viewRect, 0);
			} else {
				FractionalTrace("cache.fallback surface=%p mode=%d", surface, (int)mode);
			if (mode == B_OP_ALPHA && surface->fState.opaqueRgn.has_value()) {
				BRegion opaque;
				const BRegion &logicalOpaque = surface->fState.opaqueRgn.value();
				for (int32 i = 0; i < logicalOpaque.CountRects(); ++i) {
					BRect r = logicalOpaque.RectAt(i);
					r.OffsetBy(origin);
					opaque.Include(NativeRect(r));
				}
				opaque.IntersectWith(&targetClip);
				targetView->ConstrainClippingRegion(&opaque);
				targetView->SetDrawingMode(B_OP_COPY);

				targetView->DrawBitmap(bmp, bitmapRect, viewRect,
					mode == B_OP_ALPHA ? B_FILTER_BITMAP_BILINEAR : 0);
				BRegion remaining(targetClip);
				remaining.Exclude(&opaque);
				targetView->ConstrainClippingRegion(&remaining);
			}

			targetView->SetDrawingMode(mode);

				targetView->DrawBitmap(bmp, bitmapRect, viewRect,
					mode == B_OP_ALPHA ? B_FILTER_BITMAP_BILINEAR : 0);
			}
			targetView->ConstrainClippingRegion(&targetClip);
		}
	}

	for (HaikuSubsurface *subsurface = surface->SurfaceList().First(); subsurface != NULL; subsurface = surface->SurfaceList().GetNext(subsurface)) {
		BPoint childOrigin = origin + BPoint(subsurface->GetState().x, subsurface->GetState().y);
		DrawSurfaceTree(targetView, subsurface->Surface(), childOrigin);
	}
}

BPoint WaylandView::PopupOriginInParent(HaikuXdgPopup *popup)
{
	BPoint origin = popup->Position().LeftTop();
	HaikuXdgSurface *parent = popup->Parent();
	HaikuXdgSurface *surface = popup->XdgSurface();

	auto parentGeometry = parent->Geometry();
	if (parentGeometry.valid && !parent->HasServerDecoration())
		origin += BPoint(parentGeometry.x, parentGeometry.y);

	auto geometry = surface->Geometry();
	if (geometry.valid && !surface->HasServerDecoration())
		origin -= BPoint(geometry.x, geometry.y);

	return origin;
}

void WaylandView::DrawPopupBackground(BView *targetView, HaikuXdgPopup *popup, BPoint origin)
{
	if (popup == NULL || popup->Parent() == NULL) return;

	BPoint popupOrigin = PopupOriginInParent(popup);
	BPoint parentOrigin(origin.x - popupOrigin.x, origin.y - popupOrigin.y);
	HaikuXdgSurface *parent = popup->Parent();

	DrawPopupBackground(targetView, parent->Popup(), parentOrigin);
	DrawSurfaceTree(targetView, parent->Surface(), parentOrigin);
}

void WaylandView::Draw(BRect dirty)
{
	WaylandEnv wlEnv(this, &fActiveWlEnv);

	if (fSurface == NULL || fSurface->Subsurface() != NULL) return;

	auto viewLocked = AppKitPtrs::LockedPtr(this);

	BRect bounds = viewLocked->Bounds();
	if (!bounds.IsValid()) return;

	HaikuXdgSurface *xdgSurface = fSurface->XdgSurface();
	HaikuXdgPopup *popup = xdgSurface != NULL ? xdgSurface->Popup() : NULL;
	bool needsOffscreen = popup != NULL || !fSurface->SurfaceList().IsEmpty();

	if (!needsOffscreen) {
		DrawSurfaceTree(this, fSurface, B_ORIGIN);
	} else {
		bool isNewBitmap = false;
		if (fOffscreenBitmap == NULL || fOffscreenBitmap->Bounds() != bounds) {
			delete fOffscreenBitmap;
			fOffscreenBitmap = new(std::nothrow) BBitmap(bounds, B_RGBA32, true);
			if (fOffscreenBitmap != NULL) {
				fOffscreenView = new(std::nothrow) BView(bounds, "offscreen", B_FOLLOW_ALL, B_WILL_DRAW);
				if (fOffscreenView != NULL) {
					fOffscreenBitmap->AddChild(fOffscreenView);
					isNewBitmap = true;
				} else {
					delete fOffscreenBitmap;
					fOffscreenBitmap = NULL;
					fOffscreenView = NULL;
				}
			}
		}

		if (fOffscreenBitmap != NULL && fOffscreenBitmap->Lock()) {
			fOffscreenView->SetDrawingMode(B_OP_COPY);
			fOffscreenView->SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));

			if (isNewBitmap) {
				fOffscreenView->ConstrainClippingRegion(NULL);
				fOffscreenView->FillRect(fOffscreenView->Bounds());
			} else {
				BRegion dirtyRegion(dirty);
				fOffscreenView->ConstrainClippingRegion(&dirtyRegion);
				fOffscreenView->FillRect(dirty);
			}

			DrawPopupBackground(fOffscreenView, popup, B_ORIGIN);
			DrawSurfaceTree(fOffscreenView, fSurface, B_ORIGIN);

			fOffscreenView->ConstrainClippingRegion(NULL);
			fOffscreenView->Sync();
			fOffscreenBitmap->Unlock();

			viewLocked->SetDrawingMode(B_OP_COPY);
			viewLocked->DrawBitmap(fOffscreenBitmap, dirty, dirty);
		}
	}

	if (fSurface && fFramerateLimitDisabled)
		fSurface->CallFrameCallbacks();
}

void WaylandView::Pulse(void)
{
	if (fSurface && !fFramerateLimitDisabled)
		fSurface->CallFrameCallbacks();
}



HaikuSurface::FrameCallback *HaikuSurface::FrameCallback::Create(struct wl_client *client, uint32_t version, uint32_t id)
{
	FrameCallback *callback = new(std::nothrow) FrameCallback();
	if (!callback->Init(client, version, id)) {
		return NULL;
	}
	return callback;
}

HaikuSurface *HaikuSurface::Create(struct wl_client *client, uint32_t version, uint32_t id)
{
	HaikuSurface *surface = new(std::nothrow) HaikuSurface();
	if (!surface->Init(client, version, id)) {
		return NULL;
	}
	return surface;
}

HaikuSurface::~HaikuSurface()
{
	InvalidateNativeCache();
	HaikuSeatGlobal *seat = HaikuGetSeat(Client());
	if (seat != NULL) {
		seat->SetPointerFocus(this, false, BMessage());
		seat->SetKeyboardFocus(this, false);
	}

	if (fView != NULL) {
		Detach();
	}

	for (HaikuSubsurface *subsurface = SurfaceList().First(); subsurface != NULL; subsurface = SurfaceList().GetNext(subsurface)) {
		subsurface->fParent = NULL;
	}
}

void HaikuSurface::InvalidateNativeCache()
{
	delete fNativeCache;
	fNativeCache = NULL;
	fNativeCacheView = NULL;
	fNativeCacheSerial = 0;
	fNativeCacheSource = NULL;
}

bool HaikuSurface::EnsureNativeCache(BBitmap *source, const BRect &sourceRect,
	const BRect &destRect, drawing_mode mode)
{

	static const bool enabled = [] {
		const char* value = getenv("HAIWAY_NATIVE_CACHE");
		return value != NULL && strcmp(value, "1") == 0;
	}();
	if (!enabled) return false;
	if (source == NULL || !sourceRect.IsValid() || !destRect.IsValid()) {
		FractionalTrace("cache.skip surface=%p source=%p valid=%d source_rect=%.1f,%.1f..%.1f,%.1f dest_rect=%.1f,%.1f..%.1f,%.1f",
			this, source, source != NULL, sourceRect.left, sourceRect.top,
			sourceRect.right, sourceRect.bottom, destRect.left, destRect.top,
			destRect.right, destRect.bottom);
		return false;
	}

	if (fNativeCache != NULL && fNativeCacheSerial == fContentSerial
		&& fNativeCacheSource == source && fNativeCacheSourceRect == sourceRect
		&& fNativeCacheDestRect == destRect && fNativeCacheMode == mode) {
		FractionalTrace("cache.hit surface=%p serial=%llu", this, (unsigned long long)fContentSerial);
		return true;
	}

	FractionalTrace("cache.create surface=%p serial=%llu source=%p colorspace=%d alpha=%d source_rect=%.1f,%.1f..%.1f,%.1f dest_rect=%.1f,%.1f..%.1f,%.1f",
		this, (unsigned long long)fContentSerial, source,
		(int)source->ColorSpace(), mode == B_OP_ALPHA,
		sourceRect.left, sourceRect.top, sourceRect.right, sourceRect.bottom,
		destRect.left, destRect.top, destRect.right, destRect.bottom);
	BRect cacheBounds(0, 0, destRect.Width(), destRect.Height());
	if (fNativeCache != NULL && (fNativeCache->Bounds() != cacheBounds
		|| fNativeCache->ColorSpace() != source->ColorSpace()))
		InvalidateNativeCache();
	if (fNativeCache == NULL) {
	fNativeCache = new(std::nothrow) BBitmap(cacheBounds, source->ColorSpace(),
		true);
	if (fNativeCache == NULL || fNativeCache->InitCheck() != B_OK) {
		FractionalTrace("cache.alloc_failed surface=%p bounds=%.1fx%.1f",
			this, cacheBounds.Width() + 1, cacheBounds.Height() + 1);
		InvalidateNativeCache();
		return false;
	}
	fNativeCacheView = new(std::nothrow) BView(cacheBounds, "surface-cache",
		B_FOLLOW_NONE, B_WILL_DRAW);
	if (fNativeCacheView == NULL) {
		FractionalTrace("cache.view_alloc_failed surface=%p", this);
		InvalidateNativeCache();
		return false;
	}
	fNativeCache->AddChild(fNativeCacheView);
	}

	if (!fNativeCache->Lock()) {
		FractionalTrace("cache.lock_failed surface=%p", this);
		InvalidateNativeCache();
		return false;
	}
	bigtime_t cacheStart = system_time();


	memset(fNativeCache->Bits(), 0, fNativeCache->BitsLength());
	fNativeCacheView->SetDrawingMode(mode);
	fNativeCacheView->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	fNativeCacheView->DrawBitmap(source, sourceRect, cacheBounds,
		mode == B_OP_ALPHA ? B_FILTER_BITMAP_BILINEAR : 0);
	fNativeCacheView->Sync();
	FractionalTrace("cache.render surface=%p us=%lld", this,
		(long long)(system_time() - cacheStart));
	fNativeCache->Unlock();

	fNativeCacheSerial = fContentSerial;
	fNativeCacheSource = source;
	fNativeCacheSourceRect = sourceRect;
	fNativeCacheDestRect = destRect;
	fNativeCacheMode = mode;
	FractionalTrace("cache.ready surface=%p cache=%p pixels=%.0fx%.0f",
		this, fNativeCache, cacheBounds.Width() + 1, cacheBounds.Height() + 1);
	return true;
}

BSize HaikuSurface::Size() const
{
	if (fState.viewportDst.IsValid()) {
		return BSize(fState.viewportDst.width - 1, fState.viewportDst.height - 1);
	}
	if (fState.viewportSrc.IsValid()) {
		return BSize(fState.viewportSrc.width - 1, fState.viewportSrc.height - 1);
	}
	if (Bitmap() != NULL) {
		return BSize((Bitmap()->Bounds().Width() + 1) / fState.scale - 1,
			(Bitmap()->Bounds().Height() + 1) / fState.scale - 1);
	}
	return BSize(-1, -1);
}

void HaikuSurface::AttachWindow(BWindow *window)
{
	Assert(fView == NULL);

	window->SetPulseRate(16667);

	fView = new WaylandView(this);
	window->AddChild(fView);
	fView->MakeFocus();
}

void HaikuSurface::AttachView(BView *view)
{
	if (view == NULL) {
		fprintf(stderr, "[!] HaikuSurface::AttachView(): view == NULL\n");
		return;
	}
	fView = new WaylandView(this);
	view->AddChild(fView);
}

void HaikuSurface::AttachViewsToEarlierSubsurfaces()
{
	if (fView == NULL) {
		fprintf(stderr, "[!] HaikuSurface::AttachViewsToEarlierSubsurfaces(): fView == NULL\n");
		return;
	}
	for (HaikuSubsurface *subsurface = SurfaceList().First(); subsurface != NULL; subsurface = SurfaceList().GetNext(subsurface)) {
		subsurface->Surface()->AttachView(fView);
	}
}

void HaikuSurface::Detach()
{
	if (fView == NULL) {
		return;
	}

	fView->LockLooper();
	BLooper *looper = fView->Looper();
	fView->RemoveDescendantsAndSelf();

	fView->fSurface = NULL;
	delete fView;
	fView = NULL;

	if (looper != NULL) {
		looper->Unlock();
	}
}

void HaikuSurface::Invalidate()
{
	if (fView == NULL) {
		return;
	}

	HaikuSurface *root = fSubsurface != NULL ? fSubsurface->Root() : this;
	if (root != NULL && root->View() != NULL) {


		AppKitPtrs::LockedPtr(root->View())->Invalidate();
	}
	fDirty.MakeEmpty();
}

void HaikuSurface::CallFrameCallbacks()
{
	while (!fState.frameCallbacks.IsEmpty()) {
		FrameCallback *callback = fState.frameCallbacks.RemoveHead();
		callback->SendDone(system_time()/1000);
		callback->Destroy();
	}
}

void HaikuSurface::SetHook(Hook *hook)
{
	if (hook != NULL) {hook->fBase = this;}
	fHook.SetTo(hook);
}


void HaikuSurface::SetViewportSrc(float x, float y, float width, float height)
{
	fPendingState.viewportSrc = {x, y, width, height};
	fPendingFields |= (1 << fieldViewport);
}

void HaikuSurface::SetViewportDst(int32_t width, int32_t height)
{
	fPendingState.viewportDst = {width, height};
	fPendingFields |= (1 << fieldViewport);
}

void HaikuSurface::HandleAttach(struct wl_resource *buffer_resource, int32_t dx, int32_t dy)
{
	fContentChanged = true;
	fPendingState.buffer = HaikuShmBuffer::FromResource(buffer_resource);
	if (fPendingState.buffer != NULL) {
		BRect bounds = fPendingState.buffer->Bitmap().Bounds();
		FractionalTrace("surface.attach surface=%p buffer=%p pixels=%.0fx%.0f offset=%d,%d",
			this, fPendingState.buffer.Get(), bounds.Width() + 1, bounds.Height() + 1, dx, dy);
	} else {
		FractionalTrace("surface.attach surface=%p buffer=null offset=%d,%d", this, dx, dy);
	}
	fPendingState.dx = dx;
	fPendingState.dy = dy;
	fPendingFields |= (1 << fieldBuffer) | (1 << fieldOffset);
}

void HaikuSurface::HandleDamage(int32_t x, int32_t y, int32_t width, int32_t height)
{
	fContentChanged = true;
	width = std::min(width, 1 << 24);
	height = std::min(height, 1 << 24);
	FractionalTrace("surface.damage surface=%p logical=%d,%d %dx%d",
		this, x, y, width, height);
	fDirty.Include(BRect(x, y, x + width - 1, y + height - 1));
}

void HaikuSurface::HandleFrame(uint32_t callback_id)
{
	fPendingState.frameCallbacks.Insert(FrameCallback::Create(Client(), 1, callback_id));
	fPendingFields |= (1 << fieldFrameCallbacks);
}

void HaikuSurface::HandleSetOpaqueRegion(struct wl_resource *region_resource)
{
	if (region_resource == NULL) {
		fPendingState.opaqueRgn.reset();
	} else {
		fPendingState.opaqueRgn.emplace(HaikuRegion::FromResource(region_resource)->Region());
	}
	fPendingFields |= (1 << fieldOpaqueRgn);
}

void HaikuSurface::HandleSetInputRegion(struct wl_resource *region_resource)
{
	if (region_resource == NULL) {
		fPendingState.inputRgn.reset();
	} else {
		fPendingState.inputRgn.emplace(HaikuRegion::FromResource(region_resource)->Region());
	}
	fPendingFields |= (1 << fieldInputRgn);
}

void HaikuSurface::HandleCommit()
{



	const uint32 nonVisual = (1U << fieldFrameCallbacks) | (1U << fieldInputRgn);
	const bool repaint = fContentChanged || (fPendingFields & ~nonVisual) != 0
		|| !SurfaceList().IsEmpty();

	for (;;) {
		uint32 field = std::countr_zero(fPendingFields);
		if (field >= 32) {
			break;
		}
		fPendingFields &= ~(1U << field);
		switch (field) {
			case fieldBuffer:
				if (fState.buffer != NULL) {
					fState.buffer->SendRelease();
				}
				fState.buffer = fPendingState.buffer;
				break;
			case fieldOffset:
				fState.dx = fPendingState.dx;
				fState.dy = fPendingState.dy;
				break;
			case fieldTransform:
				fState.transform = fPendingState.transform;
				break;
			case fieldScale:
				fState.scale = fPendingState.scale;
				break;
			case fieldOpaqueRgn:
				fState.opaqueRgn = std::move(fPendingState.opaqueRgn);
				break;
			case fieldInputRgn:
				fState.inputRgn = std::move(fPendingState.inputRgn);
				break;
			case fieldFrameCallbacks:
				fState.frameCallbacks.TakeFrom(&fPendingState.frameCallbacks);
				break;
			case fieldViewport:
				fState.viewportSrc = fPendingState.viewportSrc;
				fState.viewportDst = fPendingState.viewportDst;
				break;
		}
	}
	if (fContentChanged) {
		++fContentSerial;
		fContentChanged = false;
	}

	if (View() != NULL && View()->Window() != NULL) {
		auto viewLocked = AppKitPtrs::LockedPtr(View());
		BRect previousFrame = viewLocked->Frame();
		if (fSubsurface != NULL) {
			viewLocked->MoveTo(ToNative(BPoint(fSubsurface->GetState().x, fSubsurface->GetState().y)));
		}
		BSize size = Size();
		viewLocked->ResizeTo(NativeExtent(size.width), NativeExtent(size.height));
		BRect frame = viewLocked->Frame();
		BRect bounds = viewLocked->Bounds();
		BRect windowFrame = viewLocked->Window()->Frame();
		FractionalTrace("surface.commit surface=%p buffer_scale=%d"
			" viewport_src=%.3f,%.3f %.3fx%.3f viewport_dst=%dx%d"
			" logical_size=%.0fx%.0f view_frame=%.1f,%.1f..%.1f,%.1f"
			" view_bounds=%.1f,%.1f..%.1f,%.1f window_frame=%.1f,%.1f..%.1f,%.1f",
			this, fState.scale, fState.viewportSrc.x, fState.viewportSrc.y,
			fState.viewportSrc.width, fState.viewportSrc.height,
			fState.viewportDst.width, fState.viewportDst.height,
			size.width + 1, size.height + 1,
			frame.left, frame.top, frame.right, frame.bottom,
			bounds.left, bounds.top, bounds.right, bounds.bottom,
			windowFrame.left, windowFrame.top, windowFrame.right, windowFrame.bottom);
		if (repaint || previousFrame != frame) {
			Invalidate();
		} else {
			FractionalTrace("surface.skip_unchanged surface=%p", this);
		}
	}
	if (fHook.IsSet()) {
		fHook->HandleCommit();
	}
}

void HaikuSurface::HandleSetBufferTransform(int32_t transform)
{
	fPendingState.transform = transform;
	fPendingFields |= (1 << fieldTransform);
}

void HaikuSurface::HandleSetBufferScale(int32_t scale)
{
	FractionalTrace("surface.set_buffer_scale surface=%p scale=%d", this, scale);
	if (scale <= 0) {
		wl_resource_post_error(ToResource(), WL_SURFACE_ERROR_INVALID_SCALE, "buffer scale must be positive");
		return;
	}
	fPendingState.scale = scale;
	fPendingFields |= (1 << fieldScale);
}

void HaikuSurface::HandleDamageBuffer(int32_t x, int32_t y, int32_t width, int32_t height)
{
	fContentChanged = true;
	width = std::min(width, 1 << 24);
	height = std::min(height, 1 << 24);
	FractionalTrace("surface.damage_buffer surface=%p buffer=%d,%d %dx%d",
		this, x, y, width, height);
	fDirty.Include(BRect(x, y, x + width - 1, y + height - 1));
}

void HaikuSurface::HandleOffset(int32_t x, int32_t y)
{
	fPendingState.dx = x;
	fPendingState.dy = y;
	fPendingFields |= (1 << fieldOffset);
}
