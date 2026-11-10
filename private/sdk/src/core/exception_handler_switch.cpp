/**
 * @file        core/exception_handler_switch.cpp
 * @brief       Switch (Horizon OS) ExceptionHandler stub implementation
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <rex/exception_handler.h>

namespace rex::arch {

void ExceptionHandler::Install(Handler /*fn*/, void* /*data*/) {}
void ExceptionHandler::Uninstall(Handler /*fn*/, void* /*data*/) {}

}  // namespace rex::arch
