#pragma once
#ifndef HI18N_BUILTIN_H
#define HI18N_BUILTIN_H

#include "Hi18n.h"

namespace Hi18nBuiltin {

	enum class Slot : int {
		ZhTW = 0,
		ZhCN,
		EnUS,
		JaJP,
		Count,
	};

	const char* Get(Slot slot, Hi18n::Key key);
	int SlotFromCode(const char* code);
	const char* CodeFromSlot(Slot slot);

} // namespace Hi18nBuiltin

#endif
