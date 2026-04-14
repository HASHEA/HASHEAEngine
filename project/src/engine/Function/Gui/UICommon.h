#pragma once

#include <cstdint>

namespace AshEngine
{
	using UITextureHandle = void*;

	struct UIVec2
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct UIColor
	{
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
		float a = 1.0f;
	};

	enum class UIMouseButton : uint8_t
	{
		Left = 0,
		Right = 1,
		Middle = 2
	};

	enum class UIStyleColorKind : uint8_t
	{
		Text = 0,
		WindowBg,
		ChildBg,
		PopupBg,
		Border,
		FrameBg,
		FrameBgHovered,
		FrameBgActive,
		TitleBg,
		TitleBgActive,
		MenuBarBg,
		Button,
		ButtonHovered,
		ButtonActive,
		Header,
		HeaderHovered,
		HeaderActive,
		Separator,
		SeparatorHovered,
		SeparatorActive,
		Tab,
		TabHovered,
		TabSelected,
		TableRowBg,
		TableRowBgAlt
	};

	enum class UIStyleVarKind : uint8_t
	{
		Alpha = 0,
		WindowPadding,
		WindowRounding,
		WindowBorderSize,
		FramePadding,
		FrameRounding,
		FrameBorderSize,
		ItemSpacing,
		ItemInnerSpacing,
		IndentSpacing,
		ScrollbarSize,
		GrabMinSize
	};

	struct UIContextConfig
	{
		bool enable_docking = true;
		bool enable_viewports = false;
		bool enable_keyboard_navigation = true;
		bool enable_gamepad_navigation = false;
		const char* ini_path = nullptr;
	};

	using UIConditionFlags = uint32_t;
	namespace UIConditionFlagBits
	{
		constexpr UIConditionFlags None = 0u;
		constexpr UIConditionFlags Always = 1u << 0u;
		constexpr UIConditionFlags Once = 1u << 1u;
		constexpr UIConditionFlags FirstUseEver = 1u << 2u;
		constexpr UIConditionFlags Appearing = 1u << 3u;
	}

	using UIWindowFlags = uint32_t;
	namespace UIWindowFlagBits
	{
		constexpr UIWindowFlags None = 0u;
		constexpr UIWindowFlags NoTitleBar = 1u << 0u;
		constexpr UIWindowFlags NoResize = 1u << 1u;
		constexpr UIWindowFlags NoMove = 1u << 2u;
		constexpr UIWindowFlags NoScrollbar = 1u << 3u;
		constexpr UIWindowFlags NoScrollWithMouse = 1u << 4u;
		constexpr UIWindowFlags NoCollapse = 1u << 5u;
		constexpr UIWindowFlags NoSavedSettings = 1u << 6u;
		constexpr UIWindowFlags NoInputs = 1u << 7u;
		constexpr UIWindowFlags MenuBar = 1u << 8u;
		constexpr UIWindowFlags NoDocking = 1u << 9u;
		constexpr UIWindowFlags AlwaysAutoResize = 1u << 10u;
	}

	using UIChildFlags = uint32_t;
	namespace UIChildFlagBits
	{
		constexpr UIChildFlags None = 0u;
		constexpr UIChildFlags Border = 1u << 0u;
		constexpr UIChildFlags AlwaysUseWindowPadding = 1u << 1u;
		constexpr UIChildFlags ResizeX = 1u << 2u;
		constexpr UIChildFlags ResizeY = 1u << 3u;
		constexpr UIChildFlags AutoResizeX = 1u << 4u;
		constexpr UIChildFlags AutoResizeY = 1u << 5u;
	}

	using UISelectableFlags = uint32_t;
	namespace UISelectableFlagBits
	{
		constexpr UISelectableFlags None = 0u;
		constexpr UISelectableFlags DontClosePopups = 1u << 0u;
		constexpr UISelectableFlags SpanAllColumns = 1u << 1u;
		constexpr UISelectableFlags AllowDoubleClick = 1u << 2u;
	}

	using UITreeNodeFlags = uint32_t;
	namespace UITreeNodeFlagBits
	{
		constexpr UITreeNodeFlags None = 0u;
		constexpr UITreeNodeFlags Selected = 1u << 0u;
		constexpr UITreeNodeFlags Framed = 1u << 1u;
		constexpr UITreeNodeFlags DefaultOpen = 1u << 2u;
		constexpr UITreeNodeFlags OpenOnArrow = 1u << 3u;
		constexpr UITreeNodeFlags SpanAvailWidth = 1u << 4u;
		constexpr UITreeNodeFlags Leaf = 1u << 5u;
	}

	using UITableFlags = uint32_t;
	namespace UITableFlagBits
	{
		constexpr UITableFlags None = 0u;
		constexpr UITableFlags Resizable = 1u << 0u;
		constexpr UITableFlags Reorderable = 1u << 1u;
		constexpr UITableFlags Hideable = 1u << 2u;
		constexpr UITableFlags Sortable = 1u << 3u;
		constexpr UITableFlags RowBg = 1u << 4u;
		constexpr UITableFlags Borders = 1u << 5u;
		constexpr UITableFlags BordersOuter = 1u << 6u;
		constexpr UITableFlags BordersInner = 1u << 7u;
		constexpr UITableFlags ScrollX = 1u << 8u;
		constexpr UITableFlags ScrollY = 1u << 9u;
		constexpr UITableFlags SizingStretchProp = 1u << 10u;
		constexpr UITableFlags SizingFixedFit = 1u << 11u;
	}

	using UIInputTextFlags = uint32_t;
	namespace UIInputTextFlagBits
	{
		constexpr UIInputTextFlags None = 0u;
		constexpr UIInputTextFlags ReadOnly = 1u << 0u;
		constexpr UIInputTextFlags Password = 1u << 1u;
		constexpr UIInputTextFlags EnterReturnsTrue = 1u << 2u;
		constexpr UIInputTextFlags AutoSelectAll = 1u << 3u;
		constexpr UIInputTextFlags AllowTabInput = 1u << 4u;
	}

	using UITabBarFlags = uint32_t;
	namespace UITabBarFlagBits
	{
		constexpr UITabBarFlags None = 0u;
		constexpr UITabBarFlags Reorderable = 1u << 0u;
		constexpr UITabBarFlags AutoSelectNewTabs = 1u << 1u;
		constexpr UITabBarFlags FittingPolicyResizeDown = 1u << 2u;
		constexpr UITabBarFlags FittingPolicyScroll = 1u << 3u;
	}

	using UITabItemFlags = uint32_t;
	namespace UITabItemFlagBits
	{
		constexpr UITabItemFlags None = 0u;
		constexpr UITabItemFlags UnsavedDocument = 1u << 0u;
		constexpr UITabItemFlags SetSelected = 1u << 1u;
		constexpr UITabItemFlags NoCloseWithMiddleMouseButton = 1u << 2u;
	}

}
