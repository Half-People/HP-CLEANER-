#include "HCleanTask.h"
#include <cstdio>
#include <cstring>

void HCleanTask::GetDetailSelectionSummary(char* buf, size_t buf_size) const
{
	if (buf == nullptr || buf_size == 0) {
		return;
	}
	buf[0] = '\0';

	HCleanTask* mutable_this = const_cast<HCleanTask*>(this);
	const size_t count = mutable_this->GetDetailEntryCount();
	if (count == 0) {
		return;
	}

	size_t selected = 0;
	for (size_t i = 0; i < count; ++i) {
		const HCleanDetailEntry* entry = mutable_this->GetDetailEntry(i);
		if (entry != nullptr && entry->selected) {
			++selected;
		}
	}

	char names[384] = {};
	size_t names_len = 0;
	const size_t max_labels = 3;
	size_t shown = 0;
	for (size_t i = 0; i < count && shown < max_labels; ++i) {
		const HCleanDetailEntry* entry = mutable_this->GetDetailEntry(i);
		if (entry == nullptr || !entry->selected) {
			continue;
		}
		const char* name = (entry->label != nullptr && entry->label[0] != '\0')
			? entry->label
			: entry->path;
		if (name == nullptr || name[0] == '\0') {
			continue;
		}
		if (shown > 0 && names_len + 2 < sizeof(names)) {
			names[names_len++] = ',';
			names[names_len++] = ' ';
			names[names_len] = '\0';
		}
		const int written = snprintf(names + names_len, sizeof(names) - names_len, "%s", name);
		if (written > 0) {
			names_len += static_cast<size_t>(written);
		}
		++shown;
	}

	if (selected > max_labels) {
		snprintf(buf, buf_size, "已選 %zu/%zu 項：%s…", selected, count, names);
	}
	else {
		snprintf(buf, buf_size, "已選 %zu/%zu 項：%s", selected, count, names);
	}
}

void HCleanTask::OnDetailConfigLoaded()
{
	ApplyDetailSelection();
}
