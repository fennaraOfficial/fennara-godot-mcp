(function () {
  const settingsDialog = document.querySelector("[data-settings]");
  const transcript = document.querySelector("[data-transcript]");
  const composer = document.querySelector("[data-composer]");
  const prompt = document.querySelector("[data-prompt]");
  const modelInput = document.querySelector("[data-model]");
  const modelStatus = document.querySelector("[data-model-status]");
  const appShell = document.querySelector(".app-shell");

  function openSettings() {
    if (settingsDialog && typeof settingsDialog.showModal === "function") {
      settingsDialog.showModal();
    }
  }

  function reloadUi() {
    const nextUrl = new URL(window.location.href);
    nextUrl.searchParams.set("v", String(Date.now()));
    window.location.replace(nextUrl.toString());
  }

  function appendMessage(role, text) {
    const message = document.createElement("article");
    message.className = "message " + role;

    const meta = document.createElement("div");
    meta.className = "message-meta";
    meta.innerHTML = "<span>" + (role === "user" ? "You" : "Fennara") + "</span><time>Local</time>";

    const body = document.createElement("div");
    body.className = "message-body";
    body.textContent = text;

    message.append(meta, body);
    transcript.append(message);
    transcript.scrollTop = transcript.scrollHeight;
  }

  function toggleDrawer() {
    appShell?.classList.toggle("drawer-open");
  }

  function startNewChat() {
    appShell?.classList.remove("drawer-open");
    prompt.value = "";
    prompt.focus();
  }

  document.querySelectorAll("[data-open-settings]").forEach((button) => {
    button.addEventListener("click", openSettings);
  });

  document.querySelector("[data-reload-ui]")?.addEventListener("click", reloadUi);
  document.querySelectorAll("[data-copy-code]").forEach((button) => {
    button.addEventListener("click", async () => {
      const code = button.closest(".code-block")?.querySelector("code")?.textContent ?? "";
      if (!code) {
        return;
      }
      await navigator.clipboard?.writeText(code);
      button.setAttribute("aria-label", "Copied code");
      button.setAttribute("title", "Copied");
      window.setTimeout(() => {
        button.setAttribute("aria-label", "Copy code");
        button.removeAttribute("title");
      }, 1200);
    });
  });
  document.querySelectorAll("[data-toggle-drawer]").forEach((button) => {
    button.addEventListener("click", toggleDrawer);
  });
  document.querySelectorAll("[data-new-chat]").forEach((button) => {
    button.addEventListener("click", startNewChat);
  });

  document.querySelector("[data-save-settings]")?.addEventListener("click", () => {
    const model = modelInput?.value.trim() || "openrouter/auto";
    if (modelStatus) {
      modelStatus.textContent = model;
    }
  });

  composer?.addEventListener("submit", (event) => {
    event.preventDefault();
    const text = prompt.value.trim();
    if (!text) {
      return;
    }
    appendMessage("user", text);
    appendMessage(
      "assistant",
      "Chat transport is not connected yet. Next step: wire this composer to the local daemon and OpenRouter."
    );
    prompt.value = "";
  });
})();
