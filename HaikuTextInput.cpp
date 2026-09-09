#include "HaikuScale.h"
#include "HaikuTextInput.h"
#include <text-input-unstable-v3-protocol.h>

#include <stdio.h>

#include <View.h>
#include <Input.h>
#include <AutoDeleter.h>
#include <utf8_functions.h>

#include "AppKitPtrs.h"
#include "HaikuCompositor.h"
#include "HaikuSeat.h"


enum {
	TEXT_INPUT_VERSION = 1,
};




HaikuTextInputGlobal *HaikuTextInputGlobal::Create(struct wl_display *display, HaikuSeatGlobal *seat)
{
	ObjectDeleter<HaikuTextInputGlobal> global(new(std::nothrow) HaikuTextInputGlobal(seat));
	if (!global.IsSet()) return NULL;
	if (!global->Init(display, &zwp_text_input_manager_v3_interface, TEXT_INPUT_VERSION)) return NULL;
	seat->fTextInput = global.Get();
	return global.Detach();
}

HaikuTextInputGlobal::~HaikuTextInputGlobal()
{
	fSeat->fTextInput = NULL;
}

void HaikuTextInputGlobal::Bind(struct wl_client *wl_client, uint32_t version, uint32_t id)
{

	HaikuTextInputManager *res = new(std::nothrow) HaikuTextInputManager(this);
	if (res == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	if (!res->Init(wl_client, version, id)) {
		return;
	}
}

void HaikuTextInputGlobal::Clear()
{
	fString = "";
	fSelectionBeg = -1;
	fSelectionEnd = -1;
	fActive = false;
	fConfirmed = false;
	fImReplyMsgr = BMessenger();
}

void HaikuTextInputGlobal::SendState(HaikuTextInput *textInput)
{
	if (fActive) {
		if (fConfirmed) {
			textInput->SendCommitString(fString.String());
		} else {
			textInput->SendPreeditString(fString.String(), fSelectionBeg, fSelectionEnd);
		}
	}
	textInput->SendDone(fSerial);
}

void HaikuTextInputGlobal::Enter(HaikuSurface *surface)
{
	for (HaikuTextInput *textInput = fTextInputIfaces.First(); textInput != NULL; textInput = fTextInputIfaces.GetNext(textInput)) {
		if (textInput->Client() != surface->Client()) {continue;}
		textInput->SendEnter(surface->ToResource());
	}
}

void HaikuTextInputGlobal::Leave(HaikuSurface *surface)
{
	for (HaikuTextInput *textInput = fTextInputIfaces.First(); textInput != NULL; textInput = fTextInputIfaces.GetNext(textInput)) {
		if (textInput->Client() != surface->Client()) {continue;}
		textInput->SendLeave(surface->ToResource());
	}
}

bool HaikuTextInputGlobal::MessageReceived(HaikuSurface *surface, BMessage *msg)
{


	if (fSeat->fKeyboardFocus != surface) {
		return false;
	}
	switch (msg->what) {
		case B_INPUT_METHOD_EVENT: {
			int32 opcode {};
			if (msg->FindInt32("be:opcode", &opcode) < B_OK) {
				return false;
			}
			switch (opcode) {
				case B_INPUT_METHOD_STARTED: {

					if (msg->FindMessenger("be:reply_to", &fImReplyMsgr) < B_OK) {
						Clear();
						return true;
					}
					fActive = true;

					return true;
				}
				case B_INPUT_METHOD_STOPPED: {


					for (HaikuTextInput *textInput = fTextInputIfaces.First(); textInput != NULL; textInput = fTextInputIfaces.GetNext(textInput)) {
						if (textInput->Client() != fSeat->fKeyboardFocus->Client()) continue;
						SendState(textInput);
					}
					Clear();
					return true;
				}
				case B_INPUT_METHOD_CHANGED: {


					if (msg->FindString("be:string", &fString) < B_OK) {
						fString = "";
					}
					if (
						msg->FindInt32("be:selection", 0, &fSelectionBeg) < B_OK ||
						msg->FindInt32("be:selection", 1, &fSelectionEnd) < B_OK
					) {
						fSelectionBeg = -1;
						fSelectionEnd = -1;
					}
					if (msg->FindBool("be:confirmed", &fConfirmed) < B_OK) {
						fConfirmed = false;
					}



					for (HaikuTextInput *textInput = fTextInputIfaces.First(); textInput != NULL; textInput = fTextInputIfaces.GetNext(textInput)) {
						if (textInput->Client() != fSeat->fKeyboardFocus->Client()) continue;
						SendState(textInput);
					}
					if (fConfirmed) {
						Clear();
					}

					return true;
				};
				case B_INPUT_METHOD_LOCATION_REQUEST: {

					if (!fImReplyMsgr.IsValid()) {
						return true;
					}
					int32 charCount = UTF8CountChars(fString.String(), fString.Length());
					BPoint location = ToNative(fCursorRect.LeftTop());
					float height = NativeExtent(fCursorRect.Height());
					AppKitPtrs::LockedPtr(surface->View())->ConvertToScreen(&location);

					BMessage reply(B_INPUT_METHOD_EVENT);
					reply.AddInt32("be:opcode", B_INPUT_METHOD_LOCATION_REQUEST);
					for (int32 i = 0; i < charCount; i++) {
						reply.AddPoint("be:location_reply", location);
						reply.AddFloat("be:height_reply", height);
					}

					fImReplyMsgr.SendMessage(&reply);

					return true;
				}
			}
			return false;
		}
	}
	return false;
}




void HaikuTextInputManager::HandleGetTextInput(uint32_t id, struct wl_resource *seat)
{
	HaikuTextInput *res = new(std::nothrow) HaikuTextInput(fGlobal, HaikuSeat::FromResource(seat)->GetGlobal());
	if (res == NULL) {
		wl_client_post_no_memory(Client());
		return;
	}
	if (!res->Init(Client(), Version(), id)) {
		return;
	}
}




HaikuTextInput::HaikuTextInput(HaikuTextInputGlobal *global, HaikuSeatGlobal *seat):
	fGlobal(global),
	fSeat(seat)
{
	fGlobal->fTextInputIfaces.Insert(this);
}

HaikuTextInput::~HaikuTextInput()
{
	fGlobal->fTextInputIfaces.Remove(this);
}

void HaikuTextInput::HandleEnable()
{

}

void HaikuTextInput::HandleDisable()
{

	if (fGlobal->fActive) {
		if (fGlobal->fImReplyMsgr.IsValid()) {
			BMessage reply(B_INPUT_METHOD_EVENT);
			reply.AddInt32("be:opcode", B_INPUT_METHOD_STOPPED);
			fGlobal->fImReplyMsgr.SendMessage(&reply);
		}
		fGlobal->Clear();
	}
}

void HaikuTextInput::HandleSetSurroundingText(const char *text, int32_t cursor, int32_t anchor)
{

}

void HaikuTextInput::HandleSetTextChangeCause(uint32_t cause)
{

}

void HaikuTextInput::HandleSetContentType(uint32_t hint, uint32_t purpose)
{
}

void HaikuTextInput::HandleSetCursorRectangle(int32_t x, int32_t y, int32_t width, int32_t height)
{

	fGlobal->fCursorRect = BRect(x, y, x + width - 1, y + height - 1);
}

void HaikuTextInput::HandleCommit()
{

	fGlobal->fSerial++;

	fGlobal->SendState(this);
}
