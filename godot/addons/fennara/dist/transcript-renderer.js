(function () {
  function createTranscriptRenderer(options) {
    const transcript = options.transcript;
    const markdown = options.markdown;
    const copyIcon = options.copyIcon;
    const checkIcon = options.checkIcon;
    const userCollapseChars = options.userCollapseChars || 700;
    const autoScrollThreshold = options.autoScrollThreshold || 72;

    let activeAssistant = null;
    let activeThinking = null;
    const activeTools = new Map();
    let statusLine = null;
    let pendingAssistantText = null;
    let pendingAssistantStick = false;
    let assistantRenderFrame = 0;

    function clear(resetCost, onResetCost) {
      transcript?.replaceChildren();
      activeAssistant = null;
      activeThinking = null;
      activeTools.clear();
      statusLine = null;
      clearPendingAssistantRender();
      if (resetCost) {
        onResetCost?.();
      }
    }

    function isNearBottom() {
      if (!transcript) {
        return false;
      }
      return transcript.scrollHeight - transcript.scrollTop - transcript.clientHeight <= autoScrollThreshold;
    }

    function scrollToBottom() {
      if (transcript) {
        transcript.scrollTop = transcript.scrollHeight;
      }
    }

    function chainToolBodyWheel(body) {
      if (!body || body.dataset.wheelChained === "true") {
        return;
      }
      body.dataset.wheelChained = "true";
      body.addEventListener(
        "wheel",
        (event) => {
          if (!transcript || Math.abs(event.deltaY) <= Math.abs(event.deltaX)) {
            return;
          }
          const maxScrollTop = body.scrollHeight - body.clientHeight;
          if (maxScrollTop <= 0) {
            return;
          }
          const atTop = body.scrollTop <= 0;
          const atBottom = body.scrollTop >= maxScrollTop - 1;
          const wantsPastTop = event.deltaY < 0 && atTop;
          const wantsPastBottom = event.deltaY > 0 && atBottom;
          if (!wantsPastTop && !wantsPastBottom) {
            return;
          }

          const wheelPixels =
            event.deltaMode === WheelEvent.DOM_DELTA_LINE
              ? event.deltaY * 16
              : event.deltaMode === WheelEvent.DOM_DELTA_PAGE
                ? event.deltaY * transcript.clientHeight
                : event.deltaY;
          event.preventDefault();
          transcript.scrollTop += wheelPixels;
        },
        { passive: false },
      );
    }

    function keepBottomIfNeeded(shouldStick) {
      if (shouldStick) {
        scrollToBottom();
      }
    }

    function appendMessage(role, text, attachments = []) {
      const shouldStick = isNearBottom();
      const images = normalizeAttachments(attachments);
      const message = document.createElement("article");
      message.className = "message " + role;
      message.classList.toggle("has-attachments", images.length > 0);
      message.dataset.rawText = text;

      if (images.length > 0) {
        message.append(renderAttachmentGrid(images, "message-attachments"));
      }

      const body = document.createElement("div");
      body.className = role === "assistant" ? "message-body markdown-body" : "message-body";
      body.hidden = role === "user" && !String(text || "").trim() && images.length > 0;
      if (role === "assistant") {
        renderMarkdown(body, text);
      } else {
        body.textContent = text;
      }

      message.append(body);
      if (role === "user" && text) {
        addUserCollapse(message, body, text);
      }
      transcript?.append(message);
      keepBottomIfNeeded(shouldStick);
      return message;
    }

    function appendSystem(text) {
      const shouldStick = isNearBottom();
      if (!statusLine || !statusLine.isConnected) {
        statusLine = document.createElement("p");
        statusLine.className = "chat-status-line";
        transcript?.prepend(statusLine);
      }
      statusLine.textContent = text;
      keepBottomIfNeeded(shouldStick);
    }

    function clearSystemStatus() {
      statusLine?.remove();
      statusLine = null;
    }

    function resetStreamState() {
      flushAssistantRender();
      activeAssistant = null;
      activeThinking = null;
    }

    function resetActiveAssistant() {
      flushAssistantRender();
      activeAssistant = null;
    }

    function startAssistantMessage() {
      activeAssistant = appendMessage("assistant", "");
      return activeAssistant;
    }

    function startThinkingCard() {
      const card = document.createElement("details");
      card.className = "thinking-card";
      card.open = true;
      card.innerHTML = [
        "<summary>",
        '<span class="thinking-chevron">›</span>',
        '<span class="thinking-dot"></span>',
        "<span>Thinking</span>",
        "</summary>",
        '<div class="thinking-body markdown-body"></div>',
      ].join("");
      transcript?.insertBefore(card, activeAssistant || null);
      activeThinking = card;
      return card;
    }

    function updateThinkingText(text, status) {
      const cleanText = String(text || "").trim();
      if (!cleanText && !activeThinking) {
        return;
      }
      const shouldStick = isNearBottom();
      const card = activeThinking || startThinkingCard();
      const body = card.querySelector(".thinking-body");
      if (!cleanText && body && !body.textContent.trim()) {
        if (status === "done") {
          card.remove();
          activeThinking = null;
        }
        return;
      }
      card.classList.toggle("done", status === "done");
      if (status === "done") {
        card.open = false;
      }
      if (body) {
        renderMarkdown(body, cleanText);
      }
      keepBottomIfNeeded(shouldStick);
    }

    function finishActiveThinking() {
      if (!activeThinking) {
        return;
      }
      const body = activeThinking.querySelector(".thinking-body");
      if (!body || !body.textContent.trim()) {
        activeThinking.remove();
        activeThinking = null;
        return;
      }
      activeThinking.classList.add("done");
      activeThinking.open = false;
    }

    function appendStoredThinking(text) {
      if (!String(text || "").trim()) {
        return;
      }
      const previous = activeThinking;
      activeThinking = null;
      updateThinkingText(text, "done");
      activeThinking = previous;
    }

    function updateAssistantText(text) {
      if (!activeAssistant && !String(text || "").trim()) {
        return;
      }
      const message = activeAssistant || startAssistantMessage();
      message.dataset.rawText = text;
      pendingAssistantText = text;
      pendingAssistantStick = pendingAssistantStick || isNearBottom();
      if (!assistantRenderFrame) {
        assistantRenderFrame = window.requestAnimationFrame(flushAssistantRender);
      }
    }

    function flushAssistantRender() {
      if (assistantRenderFrame) {
        window.cancelAnimationFrame(assistantRenderFrame);
        assistantRenderFrame = 0;
      }
      if (pendingAssistantText === null || !activeAssistant) {
        pendingAssistantText = null;
        pendingAssistantStick = false;
        return;
      }
      const body = activeAssistant.querySelector(".message-body");
      if (body) {
        renderMarkdown(body, pendingAssistantText);
      }
      keepBottomIfNeeded(pendingAssistantStick);
      pendingAssistantText = null;
      pendingAssistantStick = false;
    }

    function clearPendingAssistantRender() {
      if (assistantRenderFrame) {
        window.cancelAnimationFrame(assistantRenderFrame);
        assistantRenderFrame = 0;
      }
      pendingAssistantText = null;
      pendingAssistantStick = false;
    }

    function updateToolCall(item) {
      const shouldStick = isNearBottom();
      const id = item.id || "tool_call";
      let node = activeTools.get(id);
      if (!node || !node.isConnected) {
        node = document.createElement("details");
        node.className = "tool-call";
        node.open = item.status !== "done";
        node.innerHTML = [
          "<summary>",
          '<span class="tool-chevron" aria-hidden="true">›</span>',
          '<span class="tool-status" aria-hidden="true"></span>',
          "<code></code>",
          "<span></span>",
          "</summary>",
          '<div class="tool-body markdown-body"></div>',
        ].join("");
        transcript?.insertBefore(node, activeAssistant || null);
        activeTools.set(id, node);
      }

      const status = item.status || "in_progress";
      node.classList.toggle("done", status === "done");
      node.classList.toggle("failed", status === "failed");
      node.querySelector("code").textContent = item.name || "tool";
      node.querySelector("summary > span:last-child").textContent =
        status === "in_progress" ? "running" : status;
      if (status === "done" || status === "failed") {
        node.open = false;
      }

      const body = node.querySelector(".tool-body");
      if (body) {
        chainToolBodyWheel(body);
        const content = item.content || (item.arguments ? "```json\n" + item.arguments + "\n```" : "");
        renderMarkdown(body, content);
      }
      keepBottomIfNeeded(shouldStick);
    }

    function renderMarkdown(target, text) {
      target.replaceChildren();
      const normalized = normalizeMarkdown(String(text || "").replace(/\r\n/g, "\n").trim());
      if (!normalized) {
        return;
      }
      const html = markdown.render(normalized);
      target.innerHTML = window.DOMPurify.sanitize(html, {
        ADD_TAGS: ["input"],
        ADD_ATTR: ["target", "rel", "checked", "disabled", "type", "loading"],
        ALLOWED_URI_REGEXP: /^(?:(?:https?|mailto):|data:image\/(?:png|jpeg|jpg|gif|webp);base64,)/i,
      });
      target.querySelectorAll("a[href]").forEach((link) => {
        link.setAttribute("target", "_blank");
        link.setAttribute("rel", "noreferrer");
      });
      target.querySelectorAll("[data-code-copy]").forEach((button) => {
        button.addEventListener("click", async () => {
          const code = button.closest(".code-block")?.querySelector("code")?.textContent || "";
          await navigator.clipboard?.writeText(code);
          flashCopied(button, "Copy code", "Copied code");
        });
      });
    }

    function addAssistantActions(usage, formatUsageCost) {
      flushAssistantRender();
      const message = activeAssistant;
      if (!message) {
        return;
      }
      message.querySelector(".message-actions")?.remove();
      const actions = document.createElement("div");
      actions.className = "message-actions";
      actions.setAttribute("aria-label", "Message actions");

      const copy = document.createElement("button");
      copy.className = "copy-message-button";
      copy.type = "button";
      copy.setAttribute("aria-label", "Copy message");
      copy.innerHTML = copyIcon;
      copy.addEventListener("click", async () => {
        await navigator.clipboard?.writeText(message.dataset.rawText || "");
        flashCopied(copy, "Copy message", "Copied message");
      });
      actions.append(copy);

      message.append(actions);
    }

    function addActionsToMessage(message, usage, formatUsageCost) {
      const previous = activeAssistant;
      activeAssistant = message;
      addAssistantActions(usage, formatUsageCost);
      activeAssistant = previous;
    }

    function flashCopied(button, normalLabel, copiedLabel) {
      button.innerHTML = checkIcon;
      button.setAttribute("aria-label", copiedLabel);
      button.classList.add("copied");
      window.setTimeout(() => {
        button.innerHTML = copyIcon;
        button.setAttribute("aria-label", normalLabel);
        button.classList.remove("copied");
      }, 1200);
    }

    function addUserCollapse(message, body, text) {
      if (text.length <= userCollapseChars && text.split("\n").length <= 10) {
        return;
      }
      message.classList.add("is-collapsible", "is-collapsed");
      const toggle = document.createElement("button");
      toggle.className = "message-toggle";
      toggle.type = "button";
      toggle.textContent = "Show more";
      toggle.addEventListener("click", () => {
        const collapsed = message.classList.toggle("is-collapsed");
        toggle.textContent = collapsed ? "Show more" : "Show less";
        if (!collapsed) {
          body.scrollTop = 0;
        }
      });
      message.append(toggle);
    }

    function normalizeAttachments(attachments) {
      if (!Array.isArray(attachments)) {
        return [];
      }
      return attachments
        .map((image) => {
          const mime = String(image?.mime_type || image?.type || "").toLowerCase();
          const base64 = String(image?.base64 || "").trim();
          if (!base64 || !["image/png", "image/jpeg", "image/webp", "image/gif"].includes(mime)) {
            return null;
          }
          return {
            base64,
            mime_type: mime,
            name: String(image?.name || image?.description || "Attached image").slice(0, 120),
          };
        })
        .filter(Boolean);
    }

    function renderAttachmentGrid(images, className) {
      const grid = document.createElement("div");
      grid.className = className;
      grid.dataset.count = String(images.length);
      for (const image of images) {
        const figure = document.createElement("figure");
        const button = document.createElement("button");
        const img = document.createElement("img");
        button.type = "button";
        button.className = "message-attachment-button";
        button.setAttribute("aria-label", `Open ${image.name || "attached image"}`);
        img.loading = "lazy";
        img.alt = image.name || "Attached image";
        img.src = `data:${image.mime_type};base64,${image.base64}`;
        button.addEventListener("click", () => openImagePreview(img.src, img.alt));
        button.append(img);
        figure.append(button);
        grid.append(figure);
      }
      return grid;
    }

    function openImagePreview(src, alt) {
      let preview = document.querySelector("[data-image-preview]");
      if (!preview) {
        preview = document.createElement("div");
        preview.className = "image-preview";
        preview.dataset.imagePreview = "";
        preview.hidden = true;
        preview.innerHTML = [
          '<button class="image-preview-backdrop" type="button" aria-label="Close image preview" data-image-preview-close></button>',
          '<figure class="image-preview-card">',
          '<button class="image-preview-close" type="button" aria-label="Close image preview" data-image-preview-close>×</button>',
          '<img alt="" data-image-preview-img />',
          "</figure>",
        ].join("");
        preview.addEventListener("click", (event) => {
          if (event.target.closest("[data-image-preview-close]")) {
            closeImagePreview(preview);
          }
        });
        document.addEventListener("keydown", (event) => {
          if (event.key === "Escape" && !preview.hidden) {
            closeImagePreview(preview);
          }
        });
        document.body.append(preview);
      }

      const img = preview.querySelector("[data-image-preview-img]");
      if (img) {
        img.src = src;
        img.alt = alt || "Attached image";
      }
      preview.hidden = false;
      preview.querySelector(".image-preview-close")?.focus();
    }

    function closeImagePreview(preview) {
      preview.hidden = true;
      const img = preview.querySelector("[data-image-preview-img]");
      if (img) {
        img.removeAttribute("src");
      }
    }

    return {
      addActionsToMessage,
      addAssistantActions,
      appendMessage,
      appendStoredThinking,
      appendSystem,
      clear,
      clearSystemStatus,
      flashCopied,
      finishActiveThinking,
      openImagePreview,
      renderMarkdown,
      resetActiveAssistant,
      resetStreamState,
      scrollToBottom,
      updateAssistantText,
      updateThinkingText,
      updateToolCall,
    };
  }

  function normalizeMarkdown(text) {
    return text
      .replace(/^H([1-6]):\s+/gim, (_match, level) => "#".repeat(Number(level)) + " ")
      .replace(/^!\s*\[([^\]]*)\]\((https?:\/\/[^)\s]+)\)/gim, "![$1]($2)");
  }

  window.FennaraTranscriptRenderer = { createTranscriptRenderer };
})();
