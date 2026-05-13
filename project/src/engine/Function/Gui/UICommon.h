#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace AshEngine
{
	using UITextureHandle = void*;

	struct UISurfaceHandle
	{
		uint32_t value = 0;

		bool is_valid() const
		{
			return value != 0;
		}
	};

	struct UIVec2
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct UIRect
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
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

	enum class UIThemePreset : uint8_t
	{
		ClassicDark = 0,
		SlateStudio
	};

	// editor begin 修改原因：为编辑器 UI 提供默认字重与强调字重的语义化字体角色。
	enum class UIFontRole : uint8_t
	{
		Default = 0,
		Strong
	};
	// editor end

	enum class UIDirection : uint8_t
	{
		None = 0,
		Left,
		Right,
		Up,
		Down
	};

	using UIViewportId = uint32_t;
	using UIDockNodeId = uint32_t;

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
		std::string ini_path{};
		UIThemePreset theme_preset = UIThemePreset::SlateStudio;
		// editor begin 修改原因：允许编辑器控制主字体、中文合并字体、强调字体和字号策略。
		std::string font_path{};
		std::string font_merge_path{};
		std::string strong_font_path{};
		std::string strong_font_merge_path{};
		float font_size_pixels = 17.0f;
		bool use_full_chinese_glyph_range = false;
		// editor end
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
		constexpr UIWindowFlags NoBringToFrontOnFocus = 1u << 11u;
		constexpr UIWindowFlags NoNavFocus = 1u << 12u;
	}

	// editor begin 修改原因：支持编辑器为不同类型 tooltip 配置尺寸、换行和窗口行为。
	struct UITooltipConfig
	{
		UIVec2 size{};
		UIVec2 min_size{};
		UIVec2 max_size{};
		UIConditionFlags size_condition = UIConditionFlagBits::None;
		float wrap_width = 0.0f;
		UIWindowFlags window_flags = UIWindowFlagBits::None;
	};
	// editor end

	using UIDockNodeFlags = uint32_t;
	namespace UIDockNodeFlagBits
	{
		constexpr UIDockNodeFlags None = 0u;
		constexpr UIDockNodeFlags KeepAliveOnly = 1u << 0u;
		constexpr UIDockNodeFlags NoDockingOverCentralNode = 1u << 1u;
		constexpr UIDockNodeFlags PassthruCentralNode = 1u << 2u;
		constexpr UIDockNodeFlags NoDockingSplit = 1u << 3u;
		constexpr UIDockNodeFlags NoResize = 1u << 4u;
		constexpr UIDockNodeFlags AutoHideTabBar = 1u << 5u;
		constexpr UIDockNodeFlags DockSpace = 1u << 6u;
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
		constexpr UITreeNodeFlags FramePadding = 1u << 6u;
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

	using UITableColumnFlags = uint32_t;
	namespace UITableColumnFlagBits
	{
		constexpr UITableColumnFlags None = 0u;
		constexpr UITableColumnFlags Disabled = 1u << 0u;
		constexpr UITableColumnFlags DefaultHide = 1u << 1u;
		constexpr UITableColumnFlags DefaultSort = 1u << 2u;
		constexpr UITableColumnFlags WidthStretch = 1u << 3u;
		constexpr UITableColumnFlags WidthFixed = 1u << 4u;
		constexpr UITableColumnFlags NoResize = 1u << 5u;
		constexpr UITableColumnFlags NoReorder = 1u << 6u;
		constexpr UITableColumnFlags NoHide = 1u << 7u;
		constexpr UITableColumnFlags NoClip = 1u << 8u;
		constexpr UITableColumnFlags NoSort = 1u << 9u;
		constexpr UITableColumnFlags NoSortAscending = 1u << 10u;
		constexpr UITableColumnFlags NoSortDescending = 1u << 11u;
		constexpr UITableColumnFlags PreferSortAscending = 1u << 12u;
		constexpr UITableColumnFlags PreferSortDescending = 1u << 13u;
		constexpr UITableColumnFlags IndentEnable = 1u << 14u;
		constexpr UITableColumnFlags IndentDisable = 1u << 15u;
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

	enum class UIKey : uint16_t
	{
		None = 0,
		Backspace, Tab, Enter, Escape, Space,
		A, B, C, D, E, F, G, H, I, J, K, L, M,
		N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
		LeftArrow, RightArrow, UpArrow, DownArrow,
		Insert, Delete, Home, End, PageUp, PageDown
	};

	using UIModifierFlags = uint32_t;
	namespace UIModifierFlagBits
	{
		constexpr UIModifierFlags None = 0u;
		constexpr UIModifierFlags Ctrl = 1u << 0u;
		constexpr UIModifierFlags Shift = 1u << 1u;
		constexpr UIModifierFlags Alt = 1u << 2u;
		constexpr UIModifierFlags Super = 1u << 3u;
	}

	struct UIKeyChord
	{
		UIKey key = UIKey::None;
		UIModifierFlags modifiers = UIModifierFlagBits::None;
	};

	inline uint32_t make_key_chord(UIKey key, UIModifierFlags modifiers = UIModifierFlagBits::None)
	{
		return (static_cast<uint32_t>(key) & 0xFFFFu) | ((modifiers & 0xFFFFu) << 16u);
	}

	using UIDragDropFlags = uint32_t;
	namespace UIDragDropFlagBits
	{
		constexpr UIDragDropFlags None = 0u;
		constexpr UIDragDropFlags SourceAllowNullID = 1u << 0u;
		constexpr UIDragDropFlags SourceNoPreviewTooltip = 1u << 1u;
		constexpr UIDragDropFlags AcceptNoDrawDefaultRect = 1u << 2u;
		constexpr UIDragDropFlags AcceptBeforeDelivery = 1u << 3u;
		constexpr UIDragDropFlags AcceptPeekOnly = AcceptBeforeDelivery | AcceptNoDrawDefaultRect;
	}

	struct UIDragDropPayload
	{
		static constexpr int kMaxInlineDataSize = 16;

		const void* data = nullptr;
		int data_size = 0;
		bool is_preview = false;
		bool is_delivery = false;

		UIDragDropPayload() = default;

		UIDragDropPayload(const UIDragDropPayload& other)
			: data(other.data)
			, data_size(other.data_size)
			, is_preview(other.is_preview)
			, is_delivery(other.is_delivery)
		{
			std::memcpy(inline_data, other.inline_data, kMaxInlineDataSize);
			if (other.data == other.inline_data)
				data = inline_data;
		}

		UIDragDropPayload& operator=(const UIDragDropPayload& other)
		{
			if (this != &other)
			{
				data = other.data;
				data_size = other.data_size;
				is_preview = other.is_preview;
				is_delivery = other.is_delivery;
				std::memcpy(inline_data, other.inline_data, kMaxInlineDataSize);
				if (other.data == other.inline_data)
					data = inline_data;
			}
			return *this;
		}

		bool is_valid() const { return data != nullptr && data_size > 0; }

		// Copy payload data into inline buffer so it survives after
		// the ImGui drag-drop target scope ends (ImGui clears its
		// internal payload buffer on EndDragDropTarget during delivery).
		void make_data_owned()
		{
			if (data && data_size > 0 && data_size <= kMaxInlineDataSize && data != inline_data)
			{
				std::memcpy(inline_data, data, static_cast<size_t>(data_size));
				data = inline_data;
			}
		}

	private:
		alignas(8) char inline_data[kMaxInlineDataSize]{};
	};

}
