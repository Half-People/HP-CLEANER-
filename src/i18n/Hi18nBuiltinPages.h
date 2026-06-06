#pragma once
#ifndef HI18N_BUILTIN_PAGES_H
#define HI18N_BUILTIN_PAGES_H

namespace Hi18nBuiltinPages {
	struct Row {
		const char* zh_tw;
		const char* zh_cn;
		const char* en_us;
		const char* ja_jp;
	};

	const Row* Table();
	int Count();
}

#endif
