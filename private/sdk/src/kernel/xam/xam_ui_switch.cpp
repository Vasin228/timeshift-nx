/**
 * @file        kernel/xam/xam_ui_switch.cpp
 * @brief       Switch (Horizon OS) offline stubs for XAM UI dialogs
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <rex/kernel/xam/private.h>
#include <rex/logging.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>

namespace rex {
namespace kernel {
namespace xam {

using namespace rex::system;

u32 XamIsUIActive_entry() {
  return 0;
}

u32 XamShowDeviceSelectorUI_entry(u32 user_index, u32 content_type, u32 content_flags,
                                  u64 total_requested, mapped_u32 device_id_ptr,
                                  mapped_void overlapped) {
  REXKRNL_INFO("XamShowDeviceSelectorUI user={:08X} type={:08X} dev_ptr={:08X} ovlp={:08X}",
               uint32_t(user_index), uint32_t(content_type), device_id_ptr.guest_address(),
               overlapped.guest_address());
  if (device_id_ptr) {
    *device_id_ptr = 0x00000001; // Internal storage ID
  }
  if (overlapped) {
    REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped.guest_address(), X_ERROR_SUCCESS);
  }
  return X_ERROR_SUCCESS;
}

u32 XamShowMessageBoxUI_entry(u32 user_index, mapped_wstring title_ptr, mapped_wstring text_ptr,
                              u32 button_count, mapped_u32 button_ptrs, u32 active_button,
                              u32 flags, mapped_u32 result_ptr, mapped_void overlapped) {
  REXKRNL_INFO("XamShowMessageBoxUI user={:08X} buttons={} active={}",
               uint32_t(user_index), uint32_t(button_count), uint32_t(active_button));
  if (result_ptr) {
    *result_ptr = active_button;
  }
  if (overlapped) {
    REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped.guest_address(), X_ERROR_SUCCESS);
  }
  return X_ERROR_SUCCESS;
}

u32 XamShowKeyboardUI_entry(u32 user_index, u32 flags, mapped_wstring default_text,
                            mapped_wstring title, mapped_wstring description,
                            mapped_wstring result_text, u32 length, mapped_void overlapped) {
  REXKRNL_INFO("XamShowKeyboardUI user={:08X}", uint32_t(user_index));
  if (overlapped) {
    REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped.guest_address(), X_ERROR_SUCCESS);
  }
  return X_ERROR_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

REX_EXPORT(__imp__XamIsUIActive, rex::kernel::xam::XamIsUIActive_entry)
REX_EXPORT(__imp__XamShowDeviceSelectorUI, rex::kernel::xam::XamShowDeviceSelectorUI_entry)
REX_EXPORT(__imp__XamShowMessageBoxUI, rex::kernel::xam::XamShowMessageBoxUI_entry)
REX_EXPORT(__imp__XamShowKeyboardUI, rex::kernel::xam::XamShowKeyboardUI_entry)

REX_EXPORT_STUB_RETURN(__imp__XamShowAchievementsUI, 0);
REX_EXPORT_STUB_RETURN(__imp__XamShowDirtyDiscErrorUI, 0);
REX_EXPORT_STUB_RETURN(__imp__XamShowFriendsUI, 0);
REX_EXPORT_STUB_RETURN(__imp__XamShowGamerCardUIForXUID, 0);
REX_EXPORT_STUB_RETURN(__imp__XamShowMarketplaceUI, 0);
REX_EXPORT_STUB_RETURN(__imp__XamShowMessageBoxUIEx, 0);
REX_EXPORT_STUB_RETURN(__imp__XamShowPlayersUI, 0);
REX_EXPORT_STUB_RETURN(__imp__XamShowPartyUI, 0);
REX_EXPORT_STUB_RETURN(__imp__XamShowCommunitySessionsUI, 0);

REX_EXPORT_STUB(__imp__XamIsGuideDisabled);
REX_EXPORT_STUB(__imp__XamIsMessageBoxActive);
REX_EXPORT_STUB(__imp__XamIsNuiUIActive);
REX_EXPORT_STUB(__imp__XamIsSysUiInvokedByTitle);
REX_EXPORT_STUB(__imp__XamIsSysUiInvokedByXenonButton);
REX_EXPORT_STUB(__imp__XamIsUIThread);
REX_EXPORT_STUB(__imp__XamNavigate);
REX_EXPORT_STUB(__imp__XamNavigateBack);
REX_EXPORT_STUB(__imp__XamOverrideHudOpenType);
