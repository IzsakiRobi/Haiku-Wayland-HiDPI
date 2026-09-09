#pragma once

#include <Font.h>
#include <Rect.h>
#include <Size.h>
#include <cmath>



inline float DesktopScale()
{
	static const float scale = [] {



		constexpr float kNativeUiCorrection = 0.84f;
		float value = be_plain_font->Size() / 12.0f * kNativeUiCorrection;
		return std::isfinite(value) && value > 1.0f ? value : 1.0f;
	}();
	return scale;
}

inline BPoint ToNative(BPoint p) { return BPoint(p.x * DesktopScale(), p.y * DesktopScale()); }
inline BPoint ToLogical(BPoint p) { return BPoint(p.x / DesktopScale(), p.y / DesktopScale()); }


inline BRect NativeRect(BRect r)
{
	float s = DesktopScale();
	return BRect(std::round(r.left * s), std::round(r.top * s),
		std::round((r.right + 1) * s) - 1,
		std::round((r.bottom + 1) * s) - 1);
}

inline BRect LogicalRect(BRect r)
{
	float s = DesktopScale();
	return BRect(r.left / s, r.top / s,
		(r.right + 1) / s - 1, (r.bottom + 1) / s - 1);
}

inline float NativeExtent(float extent)
{
	return std::round((extent + 1) * DesktopScale()) - 1;
}

inline int32 LogicalCount(float nativeExtent)
{
	return (int32)std::round((nativeExtent + 1) / DesktopScale());
}
