// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <QString>

// The one place a core Error becomes a user sentence (coding-standards.md section 6; wording from
// docs/ui/states.md section 4).
namespace cc::ui
{

[[nodiscard]] QString errorText(const Error &error);

} // namespace cc::ui
