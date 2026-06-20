(function () {
  const DAEMON_WS_URL = "ws://127.0.0.1:41287/chat/ws";
  const PROMPT_MAX_HEIGHT = 126;
  const USER_COLLAPSE_CHARS = 700;
  const AUTO_SCROLL_THRESHOLD = 72;
  const DAEMON_RECONNECT_DELAY_MS = 250;
  const COPY_ICON = '<svg class="svg-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M6 11c0-2.83 0-4.24.88-5.12C7.76 5 9.17 5 12 5h3c2.83 0 4.24 0 5.12.88C21 6.76 21 8.17 21 11v5c0 2.83 0 4.24-.88 5.12C19.24 22 17.83 22 15 22h-3c-2.83 0-4.24 0-5.12-.88C6 20.24 6 18.83 6 16v-5Z"></path><path d="M6 19a3 3 0 0 1-3-3v-6c0-3.77 0-5.66 1.17-6.83C5.34 2 7.23 2 11 2h4a3 3 0 0 1 3 3"></path></svg>';
  const CHECK_ICON = '<svg class="svg-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="m20 6-11 11-5-5"></path></svg>';

  const settingsDialog = document.querySelector("[data-settings]");
  const modelPopover = document.querySelector("[data-model-popover]");
  const modelTrigger = document.querySelector("[data-open-model-picker]");
  const modelSearch = document.querySelector("[data-model-search]");
  const modelList = document.querySelector("[data-model-list]");
  const modelDetail = document.querySelector("[data-model-detail]");
  const customModelInput = document.querySelector("[data-custom-model]");
  const addCustomModelButton = document.querySelector("[data-add-custom-model]");
  const transcript = document.querySelector("[data-transcript]");
  const chatList = document.querySelector("[data-chat-list]");
  const chatTitle = document.querySelector("[data-chat-title]");
  const composer = document.querySelector("[data-composer]");
  const prompt = document.querySelector("[data-prompt]");
  const apiKeyInput = document.querySelector("[data-api-key]");
  const modelInput = document.querySelector("[data-model]");
  const modelStatuses = document.querySelectorAll("[data-model-status]");
  const chatSizeStatus = document.querySelector("[data-chat-size]");
  const sessionCostStatus = document.querySelector("[data-session-cost]");
  const usageContainer = document.querySelector(".composer-usage");
  const usagePopover = document.querySelector("[data-usage-popover]");
  const usageTotalCost = document.querySelector("[data-usage-total-cost]");
  const usageContextStatus = document.querySelector("[data-usage-context]");
  const reasoningEffortControls = document.querySelectorAll("[data-reasoning-effort]");
  const effortStatus = document.querySelector("[data-effort-status]");
  const effortToggle = document.querySelector("[data-effort-toggle]");
  const effortOptions = document.querySelector("[data-effort-options]");
  const effortOptionButtons = document.querySelectorAll("[data-effort-option]");
  const keyStatus = document.querySelector("[data-key-status]");
  const sendButton = document.querySelector("[data-send-button]");
  const revertButton = document.querySelector("[data-revert-button]");
  const appShell = document.querySelector(".app-shell");
  const markdown = window.markdownit({
    html: false,
    linkify: true,
    typographer: true,
    breaks: false,
  });

  if (window.markdownitTaskLists) {
    markdown.use(window.markdownitTaskLists, { enabled: false, label: false });
  }

  markdown.renderer.rules.fence = function (tokens, index, options, env, self) {
    const token = tokens[index];
    const language = token.info.trim().split(/\s+/)[0] || "text";
    const code = token.content;
    const escapedLanguage = markdown.utils.escapeHtml(language);
    const escapedCode = markdown.utils.escapeHtml(code);
    return [
      '<figure class="code-block">',
      "<figcaption>",
      `<span>${escapedLanguage}</span>`,
      '<button class="copy-code-button" type="button" aria-label="Copy code" data-code-copy>',
      COPY_ICON,
      "</button>",
      "</figcaption>",
      `<pre><code>${escapedCode}</code></pre>`,
      "</figure>",
    ].join("");
  };

  let socket = null;
  let reconnectTimer = 0;
  let requestCounter = 0;
  let activeChatId = null;
  let currentModel = "openrouter/auto";
  let currentReasoningEffort = "medium";
  let hasOpenRouterKey = false;
  let chatStreaming = false;
  let sessionCost = 0;
  let activeTurnCost = 0;
  let latestPromptTokens = 0;
  let usageCloseTimer = 0;
  let canRevert = false;
  let modelPicker = null;
  const transcriptRenderer = window.FennaraTranscriptRenderer.createTranscriptRenderer({
    transcript,
    markdown,
    copyIcon: COPY_ICON,
    checkIcon: CHECK_ICON,
    userCollapseChars: USER_COLLAPSE_CHARS,
    autoScrollThreshold: AUTO_SCROLL_THRESHOLD,
  });

  modelPicker = window.FennaraModelPicker?.createModelPicker({
    popover: modelPopover,
    trigger: modelTrigger,
    search: modelSearch,
    list: modelList,
    detail: modelDetail,
    customInput: customModelInput,
    addCustomButton: addCustomModelButton,
    getCurrentModel: () => currentModel,
    onSelect: selectModel,
    onRequestModels: () => send({ type: "list_models", request_id: nextRequestId("list-models") }),
  });

  function chatWsUrl() {
    const token = new URLSearchParams(window.location.search).get("chat_token") || "";
    return token ? DAEMON_WS_URL + "?chat_token=" + encodeURIComponent(token) : DAEMON_WS_URL;
  }

  function nextRequestId(prefix) {
    requestCounter += 1;
    return prefix + "-" + Date.now() + "-" + requestCounter;
  }

  function connect() {
    window.clearTimeout(reconnectTimer);
    socket = new WebSocket(chatWsUrl());

    socket.addEventListener("open", () => {
      appShell?.setAttribute("data-connection", "online");
      send({ type: "get_settings", request_id: nextRequestId("settings") });
      modelPicker?.requestModels();
    });

    socket.addEventListener("message", (event) => {
      let message = null;
      try {
        message = JSON.parse(event.data);
      } catch {
        return;
      }
      handleDaemonMessage(message);
    });

    socket.addEventListener("close", () => {
      appShell?.setAttribute("data-connection", "offline");
      reconnectTimer = window.setTimeout(connect, DAEMON_RECONNECT_DELAY_MS);
    });
  }

  function send(payload) {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      appendSystem("Local daemon is not connected yet.");
      return false;
    }
    socket.send(JSON.stringify(payload));
    return true;
  }

  function setStreaming(nextStreaming) {
    chatStreaming = nextStreaming;
    appShell?.classList.toggle("is-streaming", nextStreaming);
    if (sendButton) {
      sendButton.setAttribute("aria-busy", String(nextStreaming));
      sendButton.querySelector(".send-label").textContent = nextStreaming ? "Cancel" : "Send";
    }
    updateRevertButton();
  }

  function openSettings() {
    setUsagePopoverOpen(false);
    if (settingsDialog && typeof settingsDialog.showModal === "function") {
      settingsDialog.showModal();
    }
  }

  function openModelPicker() {
    setUsagePopoverOpen(false);
    if (!modelPicker?.toggle()) {
      openSettings();
    }
  }

  function reloadUi() {
    const nextUrl = new URL(window.location.href);
    nextUrl.searchParams.set("v", String(Date.now()));
    window.location.replace(nextUrl.toString());
  }

  function clearTranscript(resetCost = true) {
    transcriptRenderer.clear(resetCost, () => {
      sessionCost = 0;
      latestPromptTokens = 0;
      updateChatSize();
      updateSessionCost();
    });
    canRevert = false;
    updateRevertButton();
  }

  function appendMessage(role, text) {
    return transcriptRenderer.appendMessage(role, text);
  }

  function renderStoredMessages(messages) {
    clearTranscript(false);
    let pendingHiddenAssistantCost = 0;
    let storedPromptTokens = 0;
    for (const message of messages || []) {
      const storedUsage = parseUsage(message.usage_json);
      const promptTokens = usagePromptTokens(storedUsage);
      if (promptTokens > 0) {
        storedPromptTokens = promptTokens;
      }
      if (message.role === "assistant" && message.reasoning_content) {
        appendStoredThinking(message.reasoning_content);
      }
      if (message.role === "tool") {
        appendStoredTool(message);
        continue;
      }
      if (isStoredToolCallAssistant(message)) {
        pendingHiddenAssistantCost += storedMessageCost(message);
        continue;
      }
      const node = appendMessage(message.role, message.content || "");
      if (message.role === "assistant" && shouldShowStoredAssistantActions(message)) {
        const usage = parseUsage(message.usage_json) || { cost: message.cost };
        const visibleCost = usageCost(usage);
        const combinedCost = pendingHiddenAssistantCost + (Number.isFinite(visibleCost) ? visibleCost : 0);
        transcriptRenderer.addActionsToMessage(
          node,
          combinedCost > 0 ? { ...usage, cost: combinedCost } : usage,
          formatUsageCost,
        );
        pendingHiddenAssistantCost = 0;
      }
    }
    if (storedPromptTokens > 0) {
      latestPromptTokens = storedPromptTokens;
      updateChatSize();
    }
  }

  function isStoredToolCallAssistant(message) {
    return message.role === "assistant" &&
      !(message.content || "").trim() &&
      Boolean(message.tool_calls_json);
  }

  function shouldShowStoredAssistantActions(message) {
    return Boolean((message.content || "").trim()) || usageCost(parseUsage(message.usage_json)) > 0 || Number(message.cost) > 0;
  }

  function storedMessageCost(message) {
    const usage = parseUsage(message.usage_json);
    const cost = usageCost(usage) || Number(message.cost);
    return Number.isFinite(cost) && cost > 0 ? cost : 0;
  }

  function appendStoredTool(message) {
    const id = message.tool_call_id || message.id;
    const name = message.tool_name || "tool";
    const status = message.status === "failed" ? "failed" : "done";
    updateToolCall({
      id,
      name,
      status,
      content: message.content || "",
    });
  }

  function appendStoredThinking(text) {
    transcriptRenderer.appendStoredThinking(text);
  }

  function parseUsage(raw) {
    if (!raw) {
      return null;
    }
    try {
      return JSON.parse(raw);
    } catch {
      return null;
    }
  }

  function appendSystem(text) {
    transcriptRenderer.appendSystem(text);
  }

  function clearSystemStatus() {
    transcriptRenderer.clearSystemStatus();
  }

  function updateThinkingText(text, status) {
    transcriptRenderer.updateThinkingText(text, status);
  }

  function updateAssistantText(text) {
    transcriptRenderer.updateAssistantText(text);
  }

  function updateToolCall(item) {
    transcriptRenderer.updateToolCall(item);
  }

  function flashCopied(button, normalLabel, copiedLabel) {
    transcriptRenderer.flashCopied(button, normalLabel, copiedLabel);
  }

  function formatUsageCost(usage) {
    const cost = usageCost(usage);
    if (!Number.isFinite(cost) || cost <= 0) {
      return "";
    }
    return formatCostValue(cost);
  }

  function usageCost(usage) {
    const rawCost = usage?.cost;
    return Number(rawCost);
  }

  function usagePromptTokens(usage) {
    const value =
      usage?.prompt_tokens ?? usage?.promptTokens ?? usage?.total_tokens ?? usage?.totalTokens;
    const tokens = Number(value);
    return Number.isFinite(tokens) && tokens > 0 ? tokens : 0;
  }

  function formatTokenCount(value) {
    const tokens = Number(value);
    if (!Number.isFinite(tokens) || tokens <= 0) {
      return "0";
    }
    if (tokens < 1000) {
      return String(Math.round(tokens));
    }
    if (tokens < 1000000) {
      return (tokens / 1000).toFixed(tokens < 10000 ? 1 : 0).replace(/\.0$/, "") + "k";
    }
    return (tokens / 1000000).toFixed(tokens < 10000000 ? 1 : 0).replace(/\.0$/, "") + "M";
  }

  function updateChatSize() {
    const availableTokens = Number(modelPicker?.modelInfo(currentModel)?.context_length || 0);
    const hasAvailable = Number.isFinite(availableTokens) && availableTokens > 0;
    if (chatSizeStatus) {
      const usedText = formatTokenCount(latestPromptTokens);
      const availableText = hasAvailable ? formatTokenCount(availableTokens) : "?";
      chatSizeStatus.textContent = `${usedText} / ${availableText} tokens`;
    }
    if (usageContextStatus) {
      usageContextStatus.textContent = hasAvailable ? `${formatTokenCount(availableTokens)} tokens` : "Unknown";
    }
  }

  function updateSessionCost() {
    if (!sessionCostStatus) {
      return;
    }
    sessionCostStatus.hidden = sessionCost <= 0;
    sessionCostStatus.textContent = sessionCost > 0 ? formatCostValue(sessionCost) : "";
    sessionCostStatus.title = "";
    if (usageTotalCost) {
      usageTotalCost.textContent = sessionCost > 0 ? formatCostValue(sessionCost) : "$0.00";
    }
    if (sessionCostStatus.hidden) {
      setUsagePopoverOpen(false);
    }
  }

  function positionUsagePopover() {
    if (!usagePopover || !sessionCostStatus || usagePopover.hidden) {
      return;
    }
    const margin = 12;
    const gap = 10;
    const anchor = sessionCostStatus.getBoundingClientRect();
    const width = usagePopover.offsetWidth;
    const height = usagePopover.offsetHeight;
    const maxLeft = Math.max(margin, window.innerWidth - width - margin);
    let left = anchor.left + anchor.width / 2 - width / 2;
    left = Math.min(Math.max(left, margin), maxLeft);
    let top = anchor.top - height - gap;
    if (top < margin) {
      top = Math.min(window.innerHeight - height - margin, anchor.bottom + gap);
    }
    usagePopover.style.setProperty("--usage-popover-left", `${Math.max(margin, left)}px`);
    usagePopover.style.setProperty("--usage-popover-top", `${Math.max(margin, top)}px`);
  }

  function setUsagePopoverOpen(open) {
    if (!usagePopover || !sessionCostStatus) {
      return;
    }
    const shouldOpen = Boolean(open) && !sessionCostStatus.hidden;
    usagePopover.hidden = !shouldOpen;
    sessionCostStatus.setAttribute("aria-expanded", shouldOpen ? "true" : "false");
    if (shouldOpen) {
      positionUsagePopover();
    }
  }

  function showUsagePopover() {
    window.clearTimeout(usageCloseTimer);
    setUsagePopoverOpen(true);
  }

  function hideUsagePopoverSoon() {
    window.clearTimeout(usageCloseTimer);
    usageCloseTimer = window.setTimeout(() => setUsagePopoverOpen(false), 90);
  }

  function updateChatTitle(chat) {
    if (!chatTitle) {
      return;
    }
    chatTitle.textContent = chat?.title || "Scene Diagnostics";
  }

  function renderChatList(chats) {
    if (!chatList) {
      return;
    }
    const heading = chatList.querySelector("h2") || document.createElement("h2");
    heading.textContent = "Chats";
    chatList.replaceChildren(heading);
    for (const chat of chats || []) {
      const row = document.createElement("button");
      row.className = "chat-row";
      row.classList.toggle("active", chat.id === activeChatId);
      row.type = "button";
      row.dataset.chatId = chat.id;
      row.innerHTML = [
        '<svg class="svg-icon" viewBox="0 0 24 24" aria-hidden="true">',
        '<path d="M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8Z"></path>',
        "</svg>",
        "<span></span>",
        "<time></time>",
      ].join("");
      row.querySelector("span").textContent = chat.title || "New chat";
      row.querySelector("time").textContent = formatChatTime(chat.updated_at_ms);
      row.addEventListener("click", () => {
        send({
          type: "open_chat",
          request_id: nextRequestId("open-chat"),
          chat_id: chat.id,
        });
        appShell?.classList.remove("drawer-open");
      });
      chatList.append(row);
    }
  }

  function formatChatTime(timestampMs) {
    const deltaMs = Date.now() - Number(timestampMs || 0);
    if (!Number.isFinite(deltaMs) || deltaMs < 0) {
      return "now";
    }
    const minutes = Math.floor(deltaMs / 60000);
    if (minutes < 1) {
      return "now";
    }
    if (minutes < 60) {
      return minutes + "m";
    }
    const hours = Math.floor(minutes / 60);
    if (hours < 24) {
      return hours + "h";
    }
    return Math.floor(hours / 24) + "d";
  }

  function formatCostValue(cost) {
    if (cost > 0 && cost < 0.0001) {
      return "$" + cost.toFixed(6);
    }
    if (cost < 0.01) {
      return "$" + cost.toFixed(4);
    }
    return "$" + cost.toFixed(2);
  }

  function applySettings(settings) {
    if (!settings) {
      return;
    }
    hasOpenRouterKey = Boolean(settings.has_openrouter_key);
    currentModel = settings.model || settings.default_model || "openrouter/auto";
    currentReasoningEffort = cleanReasoningEffort(settings.reasoning_effort);
    if (modelInput) {
      modelInput.value = currentModel;
    }
    modelStatuses.forEach((status) => {
      status.textContent = currentModelLabel();
      status.title = currentModel;
    });
    updateChatSize();
    reasoningEffortControls.forEach((control) => {
      control.value = currentReasoningEffort;
    });
    updateComposerEffort();
    if (keyStatus) {
      keyStatus.textContent = hasOpenRouterKey ? "OpenRouter key saved locally" : "OpenRouter key not set";
    }
    if (apiKeyInput) {
      apiKeyInput.value = "";
      apiKeyInput.placeholder = hasOpenRouterKey ? "Saved locally. Enter a new key to replace it." : "sk-or-...";
    }

    const list = document.querySelector("#model-suggestions");
    if (list && Array.isArray(settings.text_model_suggestions)) {
      list.replaceChildren();
      for (const model of settings.text_model_suggestions) {
        const option = document.createElement("option");
        option.value = model;
        list.append(option);
      }
    }
  }

  function currentModelLabel() {
    return modelPicker?.displayName(currentModel) || currentModel;
  }

  function selectModel(modelId) {
    const clean = window.FennaraModelPicker?.cleanModelId(modelId) || String(modelId || "").trim();
    if (!clean) {
      return;
    }
    currentModel = clean;
    if (modelInput) {
      modelInput.value = clean;
    }
    modelStatuses.forEach((status) => {
      status.textContent = currentModelLabel();
      status.title = clean;
    });
    updateChatSize();
    saveCurrentChatSettings();
  }

  function cleanReasoningEffort(effort) {
    return ["low", "medium", "high"].includes(effort) ? effort : "medium";
  }

  function effortLabel(effort) {
    return effort.charAt(0).toUpperCase() + effort.slice(1);
  }

  function updateComposerEffort() {
    if (effortStatus) {
      effortStatus.textContent = effortLabel(currentReasoningEffort);
    }
    effortOptionButtons.forEach((button) => {
      const selected = button.value === currentReasoningEffort;
      button.setAttribute("aria-selected", String(selected));
    });
  }

  function setEffortMenuOpen(open) {
    if (!effortOptions || !effortToggle) {
      return;
    }
    effortOptions.hidden = !open;
    effortToggle.setAttribute("aria-expanded", String(open));
  }

  function saveCurrentChatSettings() {
    const payload = {
      type: "save_settings",
      request_id: nextRequestId("silent-settings"),
      model: cleanUiModelId(modelInput?.value || currentModel),
      reasoning_effort: currentReasoningEffort,
    };
    return send(payload);
  }

  function handleDaemonMessage(message) {
    if (message.type === "settings" || message.type === "settings_saved") {
      applySettings(message.settings);
      if (message.type === "settings_saved") {
        if (!String(message.request_id || "").startsWith("silent-settings")) {
          appendSystem("Settings saved locally.");
          window.setTimeout(clearSystemStatus, 1200);
        }
      } else {
        clearSystemStatus();
      }
      return;
    }
    if (message.type === "chat_reset") {
      clearTranscript();
      setStreaming(false);
      return;
    }
    if (message.type === "chat_list") {
      renderChatList(message.chats || []);
      return;
    }
    if (message.type === "model_list") {
      modelPicker?.applyCatalog(message.catalog);
      modelStatuses.forEach((status) => {
        status.textContent = currentModelLabel();
        status.title = currentModel;
      });
      updateChatSize();
      return;
    }
    if (message.type === "chat_opened") {
      activeChatId = message.chat?.id || null;
      updateChatTitle(message.chat);
      currentModel = message.chat?.model || currentModel;
      currentReasoningEffort = cleanReasoningEffort(message.chat?.reasoning_effort || currentReasoningEffort);
      modelStatuses.forEach((status) => {
        status.textContent = currentModelLabel();
        status.title = currentModel;
      });
      reasoningEffortControls.forEach((control) => {
        control.value = currentReasoningEffort;
      });
      updateComposerEffort();
      renderStoredMessages(message.messages || []);
      if (message.reverted && typeof message.restored_message === "string" && prompt) {
        prompt.value = message.restored_message;
        resizePrompt();
        prompt.focus();
      }
      canRevert = Boolean(message.can_revert);
      updateRevertButton();
      sessionCost = Number(message.chat?.total_cost || 0);
      latestPromptTokens = Number(message.chat?.latest_prompt_tokens || latestPromptTokens || 0);
      updateChatSize();
      updateSessionCost();
      return;
    }
    if (message.type === "chat_created") {
      activeChatId = message.chat?.id || activeChatId;
      updateChatTitle(message.chat);
      currentModel = message.chat?.model || currentModel;
      sessionCost = Number(message.chat?.total_cost || 0);
      latestPromptTokens = Number(message.chat?.latest_prompt_tokens || 0);
      updateChatSize();
      updateSessionCost();
      return;
    }
    if (message.type === "chat_updated") {
      if (message.chat?.id && (!activeChatId || message.chat.id === activeChatId)) {
        activeChatId = message.chat.id;
        updateChatTitle(message.chat);
        const nextSessionCost = Number(message.chat?.total_cost || sessionCost);
        if (chatStreaming && Number.isFinite(nextSessionCost) && nextSessionCost > sessionCost) {
          activeTurnCost += nextSessionCost - sessionCost;
        }
        sessionCost = nextSessionCost;
        latestPromptTokens = Number(message.chat?.latest_prompt_tokens || latestPromptTokens || 0);
        updateChatSize();
        updateSessionCost();
      }
      return;
    }
    if (message.type === "chat_stream_start") {
      clearSystemStatus();
      setStreaming(true);
      transcriptRenderer.resetStreamState();
      activeTurnCost = 0;
      activeChatId = message.chat_id || activeChatId;
      if (message.user_message) {
        appendMessage("user", message.user_message.content || "");
      }
      canRevert = Boolean(message.can_revert);
      updateRevertButton();
      return;
    }
    if (message.type === "chat_item_update" && message.item?.type === "message") {
      updateAssistantText(message.item.content || "");
      return;
    }
    if (message.type === "chat_item_update" && message.item?.type === "reasoning") {
      updateThinkingText(message.item.content || "", message.item.status);
      return;
    }
    if (
      message.type === "chat_item_update" &&
      (message.item?.type === "function_call" || message.item?.type === "tool_result")
    ) {
      updateToolCall(message.item);
      if (message.item?.type === "tool_result") {
        transcriptRenderer.resetActiveAssistant();
      }
      return;
    }
    if (message.type === "chat_response") {
      clearSystemStatus();
      updateAssistantText(message.response || "");
      const cost = usageCost(message.usage);
      if (Number.isFinite(cost)) {
        activeTurnCost += cost;
        sessionCost += cost;
        updateSessionCost();
      }
      latestPromptTokens = usagePromptTokens(message.usage) || latestPromptTokens;
      updateChatSize();
      const turnUsage = { ...(message.usage || {}), cost: activeTurnCost };
      transcriptRenderer.addAssistantActions(turnUsage, formatUsageCost);
      activeTurnCost = 0;
      transcriptRenderer.resetActiveAssistant();
      setStreaming(false);
      updateRevertButton();
      return;
    }
    if (message.type === "chat_cancelled") {
      clearSystemStatus();
      updateAssistantText(message.response || "");
      transcriptRenderer.resetActiveAssistant();
      setStreaming(false);
      canRevert = Boolean(message.can_revert ?? true);
      updateRevertButton();
      appendSystem("Cancelled.");
      window.setTimeout(clearSystemStatus, 1200);
      return;
    }
    if (message.type === "error") {
      appendSystem(message.message || "Chat request failed.");
      transcriptRenderer.resetActiveAssistant();
      setStreaming(false);
      updateRevertButton();
      if (message.code === "missing_openrouter_key") {
        openSettings();
      }
    }
  }

  function updateRevertButton() {
    if (!revertButton) {
      return;
    }
    revertButton.disabled = chatStreaming || !canRevert || !activeChatId;
  }

  function toggleDrawer() {
    appShell?.classList.toggle("drawer-open");
  }

  function closeDrawer() {
    appShell?.classList.remove("drawer-open");
  }

  function closeDrawerFromOutsideClick(event) {
    if (!appShell?.classList.contains("drawer-open")) {
      return;
    }
    if (event.target.closest("[data-chat-drawer]") || event.target.closest("[data-toggle-drawer]")) {
      return;
    }
    closeDrawer();
  }

  function startNewChat() {
    closeDrawer();
    clearTranscript(true);
    send({ type: "new_chat", request_id: nextRequestId("new-chat") });
    prompt.value = "";
    resizePrompt();
    prompt.focus();
  }

  document.querySelectorAll("[data-open-settings]").forEach((button) => {
    button.addEventListener("click", openSettings);
  });
  document.querySelectorAll("[data-open-model-picker]").forEach((button) => {
    button.addEventListener("click", openModelPicker);
  });

  document.querySelector("[data-reload-ui]")?.addEventListener("click", reloadUi);
  document.querySelectorAll("[data-copy-code]").forEach((button) => {
    button.addEventListener("click", async () => {
      const code = button.closest(".code-block")?.querySelector("code")?.textContent ?? "";
      if (!code) {
        return;
      }
      await navigator.clipboard?.writeText(code);
      flashCopied(button, "Copy code", "Copied code");
    });
  });
  document.querySelectorAll("[data-toggle-drawer]").forEach((button) => {
    button.addEventListener("click", toggleDrawer);
  });
  document.querySelectorAll("[data-new-chat]").forEach((button) => {
    button.addEventListener("click", startNewChat);
  });
  usageContainer?.addEventListener("mouseenter", showUsagePopover);
  usageContainer?.addEventListener("mouseleave", hideUsagePopoverSoon);
  usagePopover?.addEventListener("mouseenter", showUsagePopover);
  usagePopover?.addEventListener("mouseleave", hideUsagePopoverSoon);
  sessionCostStatus?.addEventListener("focus", showUsagePopover);
  sessionCostStatus?.addEventListener("blur", hideUsagePopoverSoon);
  revertButton?.addEventListener("click", () => {
    if (chatStreaming || !activeChatId) {
      return;
    }
    send({
      type: "revert_chat",
      request_id: nextRequestId("revert-chat"),
      chat_id: activeChatId,
    });
  });
  sendButton?.addEventListener("click", (event) => {
    if (!chatStreaming) {
      return;
    }
    event.preventDefault();
    requestCancel();
  });

  function requestCancel() {
    if (!activeChatId) {
      return;
    }
    appendSystem("Cancelling...");
    const cancelSocket = new WebSocket(chatWsUrl());
    cancelSocket.addEventListener("open", () => {
      cancelSocket.send(JSON.stringify({
        type: "cancel_chat",
        request_id: nextRequestId("cancel-chat"),
        chat_id: activeChatId,
      }));
      window.setTimeout(() => cancelSocket.close(), 120);
    });
    cancelSocket.addEventListener("error", () => {
      appendSystem("Cancel request failed.");
    });
  }

  document.querySelector("[data-save-settings]")?.addEventListener("click", (event) => {
    event.preventDefault();
    const payload = {
      type: "save_settings",
      request_id: nextRequestId("save-settings"),
      model: cleanUiModelId(modelInput?.value || currentModel),
      reasoning_effort: currentReasoningEffort,
    };
    const key = apiKeyInput?.value.trim();
    if (key) {
      payload.openrouter_api_key = key;
    }
    if (send(payload)) {
      settingsDialog?.close();
    }
  });

  reasoningEffortControls.forEach((control) => {
    control.addEventListener("change", () => {
      currentReasoningEffort = cleanReasoningEffort(control.value);
      reasoningEffortControls.forEach((nextControl) => {
        nextControl.value = currentReasoningEffort;
      });
      updateComposerEffort();
      saveCurrentChatSettings();
    });
  });
  effortToggle?.addEventListener("click", (event) => {
    event.stopPropagation();
    setEffortMenuOpen(effortOptions?.hidden !== false);
  });
  effortOptionButtons.forEach((button) => {
    button.addEventListener("click", (event) => {
      event.stopPropagation();
      currentReasoningEffort = cleanReasoningEffort(button.value);
      reasoningEffortControls.forEach((control) => {
        control.value = currentReasoningEffort;
      });
      updateComposerEffort();
      setEffortMenuOpen(false);
      saveCurrentChatSettings();
    });
  });
  document.addEventListener("click", (event) => {
    closeDrawerFromOutsideClick(event);
    setEffortMenuOpen(false);
    setUsagePopoverOpen(false);
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      setEffortMenuOpen(false);
      setUsagePopoverOpen(false);
      closeDrawer();
    }
  });
  window.addEventListener("resize", positionUsagePopover);
  window.addEventListener("scroll", positionUsagePopover, true);

  composer?.addEventListener("submit", (event) => {
    event.preventDefault();
    if (chatStreaming) {
      return;
    }
    const text = prompt.value.trim();
    if (!text) {
      return;
    }
    if (!hasOpenRouterKey) {
      openSettings();
      return;
    }
    const model = cleanUiModelId(modelInput?.value || currentModel);
    currentReasoningEffort = cleanReasoningEffort(currentReasoningEffort);
    transcriptRenderer.resetStreamState();
    send({
      type: "send_chat",
      request_id: nextRequestId("chat"),
      chat_id: activeChatId,
      message: text,
      model,
      reasoning_effort: currentReasoningEffort,
    });
    prompt.value = "";
    resizePrompt();
  });

  function cleanUiModelId(modelId) {
    return window.FennaraModelPicker?.cleanModelId(modelId) || String(modelId || "").trim();
  }

  function resizePrompt() {
    if (!prompt) {
      return;
    }
    prompt.style.height = "auto";
    const nextHeight = Math.min(prompt.scrollHeight, PROMPT_MAX_HEIGHT);
    prompt.style.height = nextHeight + "px";
    prompt.style.overflowY = prompt.scrollHeight > PROMPT_MAX_HEIGHT ? "auto" : "hidden";
  }

  prompt?.addEventListener("keydown", (event) => {
    if (event.key !== "Enter" || event.shiftKey || event.ctrlKey || event.altKey || event.metaKey) {
      return;
    }
    event.preventDefault();
    composer?.requestSubmit();
  });
  prompt?.addEventListener("input", resizePrompt);
  prompt?.addEventListener("paste", () => window.setTimeout(resizePrompt, 0));

  clearTranscript();
  appendSystem("Connecting to local daemon...");
  resizePrompt();
  updateChatSize();
  updateSessionCost();
  connect();
})();
