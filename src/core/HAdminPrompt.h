#pragma once
#ifndef HADMIN_PROMPT_H
#define HADMIN_PROMPT_H

// ImGui 管理員權限提示（啟動時與清理／磁碟健康等場景）
namespace HAdminPrompt {

	enum class Scene {
		Startup,
		Clean,
		DiskHealth,
		Optimize,
		Manual,
	};

	enum class Result {
		None,
		ContinueWithoutAdmin,
		ElevateNewInstance,
		Cancel,
	};

	void QueueStartupIfNeeded();
	void Queue(Scene scene);
	void Render();

	// 同步閘道：已可繼續 true；尚等待或已取消／已發起提權 false
	bool TryGate(Scene scene);

	Result PollResult();
	bool IsModalOpen();
}

#endif
