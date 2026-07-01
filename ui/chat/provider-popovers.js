(function () {
  function noop() {}

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function createProviderPopovers(options = {}) {
    const elements = options.elements || {};
    const callbacks = options.callbacks || {};
    const settings = options.settings || {};
    const modelPopover = elements.modelPopover || null;
    const providerPopover = elements.providerPopover || null;
    const providerKeyPopover = elements.providerKeyPopover || null;
    const ollamaSetupPopover = elements.ollamaSetupPopover || null;
    const providerOptionsList = elements.providerOptionsList || null;
    const providerSearch = elements.providerSearch || null;
    const providerKeyTitle = elements.providerKeyTitle || null;
    const providerKeyInlineInput = elements.providerKeyInlineInput || null;
    const ollamaBaseUrlInput = elements.ollamaBaseUrlInput || null;
    const localSetupTitle = elements.localSetupTitle || null;
    const localSetupHelp = elements.localSetupHelp || null;
    const defaultOllamaBaseUrl = settings.defaultOllamaBaseUrl || "";
    const defaultLocalBaseUrls = settings.defaultLocalBaseUrls || {};
    const ensureDaemonConnected = callbacks.ensureDaemonConnected || (() => true);
    const setUsagePopoverOpen = callbacks.setUsagePopoverOpen || noop;
    const closeCommandPalette = callbacks.closeCommandPalette || noop;
    const getModelPicker = callbacks.getModelPicker || (() => null);
    const getProviderRegistry = callbacks.getProviderRegistry || (() => []);
    const getProviderMetadata = callbacks.getProviderMetadata || (() => new Map());
    const getCurrentProvider = callbacks.getCurrentProvider || (() => "");
    const setKeyPromptProvider = callbacks.setKeyPromptProvider || noop;
    const requestModelList = callbacks.requestModelList || noop;
    const providerBaseUrl = callbacks.providerBaseUrl || (() => "");
    const providerStatusLabel = callbacks.providerStatusLabel || (() => "");
    const providerUsesBaseUrlSetup = callbacks.providerUsesBaseUrlSetup || (() => false);
    const chooseProvider = callbacks.chooseProvider || noop;

    function openModelPicker(forceOpen = false) {
      if (!ensureDaemonConnected()) {
        return;
      }
      const modelPicker = getModelPicker();
      setUsagePopoverOpen(false);
      closeProviderPicker();
      closeOpenRouterKeyPrompt();
      closeOllamaSetupPrompt();
      closeCommandPalette();
      if (forceOpen && modelPicker?.open()) {
        return;
      }
      if (!modelPicker?.toggle()) {
        openProviderPicker();
      }
    }

    function openProviderPicker() {
      if (!ensureDaemonConnected()) {
        return false;
      }
      const modelPicker = getModelPicker();
      setUsagePopoverOpen(false);
      modelPicker?.close();
      closeOpenRouterKeyPrompt();
      closeOllamaSetupPrompt();
      closeCommandPalette();
      if (!providerPopover) {
        return false;
      }
      providerPopover.setAttribute("tabindex", "-1");
      providerPopover.hidden = false;
      providerPopover.focus({ preventScroll: true });
      requestModelList({ refreshOllama: true });
      renderProviderOptions();
      positionProviderPopover();
      window.setTimeout(() => providerSearch?.focus({ preventScroll: true }), 0);
      return true;
    }

    function closeProviderPicker() {
      if (!providerPopover || providerPopover.hidden) {
        return;
      }
      providerPopover.hidden = true;
    }

    function centerPopover(popover, width = 560) {
      if (!popover) {
        return;
      }
      const viewportPad = 10;
      const nextWidth = Math.min(width, Math.max(300, window.innerWidth - viewportPad * 2));
      const height = popover.offsetHeight || 180;
      popover.style.width = nextWidth + "px";
      popover.style.left = Math.max(viewportPad, (window.innerWidth - nextWidth) / 2) + "px";
      popover.style.top = Math.max(viewportPad, (window.innerHeight - height) / 2) + "px";
      popover.dataset.side = "center";
    }

    function positionProviderPopover() {
      centerPopover(providerPopover, 560);
    }

    function openOpenRouterKeyPrompt() {
      return openProviderKeyPrompt("openrouter");
    }

    function openProviderKeyPrompt(providerId) {
      if (!ensureDaemonConnected()) {
        return false;
      }
      const providerMetadata = getProviderMetadata();
      const provider = providerMetadata.get(providerId) || providerMetadata.get("openrouter");
      const modelPicker = getModelPicker();
      setUsagePopoverOpen(false);
      modelPicker?.close();
      closeProviderPicker();
      closeOllamaSetupPrompt();
      closeCommandPalette();
      if (!providerKeyPopover || !provider) {
        return false;
      }
      setKeyPromptProvider(provider.id);
      if (providerKeyTitle) {
        providerKeyTitle.textContent = `${provider.name} API key`;
      }
      if (providerKeyInlineInput) {
        providerKeyInlineInput.value = "";
        providerKeyInlineInput.placeholder = provider.auth?.env || "API key";
      }
      providerKeyPopover.setAttribute("tabindex", "-1");
      providerKeyPopover.hidden = false;
      providerKeyPopover.focus({ preventScroll: true });
      positionProviderKeyPrompt();
      window.setTimeout(() => providerKeyInlineInput?.focus(), 0);
      return true;
    }

    function closeOpenRouterKeyPrompt() {
      if (!providerKeyPopover || providerKeyPopover.hidden) {
        return;
      }
      providerKeyPopover.hidden = true;
    }

    function openOllamaSetupPrompt() {
      if (!ensureDaemonConnected()) {
        return false;
      }
      const modelPicker = getModelPicker();
      setUsagePopoverOpen(false);
      modelPicker?.close();
      closeProviderPicker();
      closeOpenRouterKeyPrompt();
      closeCommandPalette();
      if (!ollamaSetupPopover) {
        return false;
      }
      syncOllamaSetupFields();
      ollamaSetupPopover.setAttribute("tabindex", "-1");
      ollamaSetupPopover.hidden = false;
      ollamaSetupPopover.focus({ preventScroll: true });
      positionOllamaSetupPrompt();
      window.setTimeout(() => ollamaBaseUrlInput?.focus({ preventScroll: true }), 0);
      return true;
    }

    function closeOllamaSetupPrompt() {
      if (!ollamaSetupPopover || ollamaSetupPopover.hidden) {
        return;
      }
      ollamaSetupPopover.hidden = true;
    }

    function positionProviderKeyPrompt() {
      centerPopover(providerKeyPopover, 420);
    }

    function positionOllamaSetupPrompt() {
      centerPopover(ollamaSetupPopover, 420);
    }

    function renderProviderOptions() {
      const query = String(providerSearch?.value || "").trim().toLowerCase();
      if (!providerOptionsList) {
        return;
      }
      const ranked = getProviderRegistry()
        .map((provider, index) => ({
          provider,
          index,
          label: `${provider.name} ${provider.id}`.toLowerCase(),
        }))
        .map((item) => ({ ...item, score: providerScore(item.label, query) }))
        .filter((item) => item.score < 99)
        .sort((a, b) => a.score - b.score || a.index - b.index);
      providerOptionsList.replaceChildren();
      ranked.forEach(({ provider }) => {
        providerOptionsList.append(renderProviderRow(provider));
      });
      if (!ranked.length) {
        const empty = document.createElement("div");
        empty.className = "model-empty";
        const text = document.createElement("p");
        text.textContent = "No matching providers.";
        empty.append(text);
        providerOptionsList.append(empty);
      }
      positionProviderPopover();
    }

    function renderProviderRow(provider) {
      const canUpdateKey = provider.auth?.type === "api_key" && provider.connected;
      const status = canUpdateKey ? "Connected - update key" : providerStatusLabel(provider);
      const row = document.createElement("button");
      row.className = "provider-row";
      row.type = "button";
      row.dataset.providerOption = provider.id;
      row.title = canUpdateKey
        ? `Update ${provider.name} API key`
        : `Use ${provider.name}`;
      row.innerHTML = [
        "<span>",
        `<strong>${escapeHtml(provider.name)}</strong>`,
        "</span>",
        `<b>${escapeHtml(status)}</b>`,
      ].join("");
      row.addEventListener("click", (event) => {
        event.stopPropagation();
        if (canUpdateKey) {
          chooseProvider(provider.id);
          openProviderKeyPrompt(provider.id);
          return;
        }
        chooseProvider(provider.id);
      });
      return row;
    }

    function syncOllamaSetupFields() {
      const providerMetadata = getProviderMetadata();
      const provider = providerMetadata.get(getCurrentProvider()) || providerMetadata.get("ollama");
      const defaultBaseUrl = provider?.setup?.default_base_url || defaultLocalBaseUrls[provider?.id] || defaultOllamaBaseUrl;
      const baseUrl = providerBaseUrl(provider?.id || "ollama");
      if (localSetupTitle) {
        localSetupTitle.textContent = provider?.name || "Local provider";
      }
      if (localSetupHelp) {
        localSetupHelp.textContent = `${provider?.name || "This provider"} uses ${defaultBaseUrl} by default. Override it only if your local server uses another URL. Models are listed automatically from the local server.`;
      }
      if (ollamaBaseUrlInput) {
        ollamaBaseUrlInput.placeholder = defaultBaseUrl;
        ollamaBaseUrlInput.value = baseUrl === defaultBaseUrl ? "" : baseUrl;
      }
    }

    function providerScore(label, query) {
      if (!query) {
        return 0;
      }
      if (label === query) {
        return 0;
      }
      if (label.startsWith(query)) {
        return 1;
      }
      if (label.split(/[\s()_-]+/).some((part) => part.startsWith(query))) {
        return 2;
      }
      if (label.includes(query)) {
        return 3;
      }
      return 99;
    }

    return {
      closeOllamaSetupPrompt,
      closeOpenRouterKeyPrompt,
      closeProviderPicker,
      openModelPicker,
      openOllamaSetupPrompt,
      openOpenRouterKeyPrompt,
      openProviderKeyPrompt,
      openProviderPicker,
      positionOllamaSetupPrompt,
      positionProviderKeyPrompt,
      positionProviderPopover,
      renderProviderOptions,
      syncOllamaSetupFields,
    };
  }

  window.FennaraProviderPopovers = {
    createProviderPopovers,
  };
})();
