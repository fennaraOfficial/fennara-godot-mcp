(function () {
  function createTranscriptRenderer(options) {
    const transcript = options.transcript;
    const markdown = options.markdown;
    const copyIcon = options.copyIcon;
    const checkIcon = options.checkIcon;
    const onProjectFileReference = options.onProjectFileReference;
    const onToolApprovalReview = options.onToolApprovalReview;
    const userCollapseChars = options.userCollapseChars || 700;
    const autoScrollThreshold = options.autoScrollThreshold || 72;

    let activeAssistant = null;
    let activeThinking = null;
    const activeTools = new Map();
    let activeToolAnchor = null;
    let statusLine = null;
    let pendingAssistantText = null;
    let pendingAssistantStick = false;
    let assistantRenderFrame = 0;
    let streamActive = false;
    let streamFollowing = false;

    transcript?.addEventListener(
      "scroll",
      () => {
        if (!streamActive) {
          return;
        }
        streamFollowing = isNearBottom();
      },
      { passive: true },
    );

    function clear(resetCost, onResetCost) {
      transcript?.replaceChildren();
      activeAssistant = null;
      activeThinking = null;
      activeTools.clear();
      activeToolAnchor = null;
      statusLine = null;
      streamActive = false;
      streamFollowing = false;
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
      if (streamActive ? streamFollowing : shouldStick) {
        scrollToBottom();
      }
    }

    function beginStream() {
      flushAssistantRender();
      streamActive = true;
      streamFollowing = isNearBottom();
      keepBottomIfNeeded(streamFollowing);
    }

    function endStream() {
      flushAssistantRender();
      keepBottomIfNeeded(streamFollowing);
      streamActive = false;
      streamFollowing = false;
    }

    function appendMessage(role, text, attachments = [], contextSnippets = []) {
      const shouldStick = isNearBottom();
      const images = normalizeAttachments(attachments);
      const contexts = normalizeContextSnippets(contextSnippets);
      const message = document.createElement("article");
      message.className = "message " + role;
      message.classList.toggle("has-attachments", images.length > 0 || contexts.length > 0);
      message.dataset.rawText = text;

      if (images.length > 0) {
        message.append(renderAttachmentGrid(images, "message-attachments"));
      }
      if (contexts.length > 0) {
        message.append(renderContextGrid(contexts, "message-contexts"));
      }

      const body = document.createElement("div");
      body.className = role === "assistant" ? "message-body markdown-body" : "message-body";
      body.hidden = role === "user" && !String(text || "").trim() && (images.length > 0 || contexts.length > 0);
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
      if (role === "assistant") {
        activeAssistant = message;
      } else {
        activeAssistant = null;
      }
      activeToolAnchor = null;
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
      activeToolAnchor = null;
    }

    function resetActiveAssistant() {
      flushAssistantRender();
      activeAssistant = null;
      activeToolAnchor = null;
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
        flushAssistantRender();
        node = document.createElement("details");
        node.className = "tool-call";
        node.open = !isTerminalToolStatus(item.status || "in_progress");
        node.innerHTML = [
          "<summary>",
          '<span class="tool-chevron" aria-hidden="true">›</span>',
          '<span class="tool-status" aria-hidden="true"></span>',
          "<code></code>",
          "<span></span>",
          "</summary>",
          '<div class="tool-body markdown-body"></div>',
        ].join("");
        insertToolNode(node);
        activeTools.set(id, node);
      }

      const status = item.status || "in_progress";
      node.classList.toggle("done", status === "done" || status === "completed");
      node.classList.toggle("failed", status === "failed");
      node.classList.toggle("timed-out", status === "timed_out");
      node.classList.toggle("cancelled", status === "cancelled");
      node.classList.toggle("denied", status === "denied");
      node.classList.toggle("pending-approval", status === "pending_approval");
      node.querySelector("code").textContent = item.name || "tool";
      node.querySelector("summary > span:last-child").textContent =
        toolStatusLabel(status);
      if (isTerminalToolStatus(status)) {
        node.open = false;
      } else if (status === "pending_approval") {
        node.open = true;
      }

      const body = node.querySelector(".tool-body");
      if (body) {
        chainToolBodyWheel(body);
        const content = item.content || approvalMarkdown(item.approval) || (item.arguments ? "```json\n" + item.arguments + "\n```" : "");
        renderMarkdown(body, content);
        renderToolApproval(body, item.approval);
      }
      keepBottomIfNeeded(shouldStick);
    }

    function isTerminalToolStatus(status) {
      return status === "done" ||
        status === "completed" ||
        status === "failed" ||
        status === "timed_out" ||
        status === "cancelled" ||
        status === "denied";
    }

    function insertToolNode(node) {
      if (!transcript) {
        return;
      }
      if (activeToolAnchor?.isConnected) {
        activeToolAnchor.after(node);
      } else if (activeAssistant?.isConnected) {
        activeAssistant.after(node);
      } else {
        transcript.append(node);
      }
      activeToolAnchor = node;
    }

    function toolStatusLabel(status) {
      if (status === "preparing") {
        return "preparing";
      }
      if (status === "queued" || status === "pending") {
        return "queued";
      }
      if (status === "in_progress" || status === "executing" || status === "running") {
        return "running";
      }
      if (status === "done" || status === "completed") {
        return "done";
      }
      if (status === "pending_approval") {
        return "needs approval";
      }
      if (status === "timed_out") {
        return "timed out";
      }
      return status;
    }

    function approvalMarkdown(approval) {
      if (!approval || approval.status !== "pending_approval") {
        return "";
      }
      const summary = approval.summary ? `\n\n${approval.summary}` : "";
      return `Approval required: ${approval.reason || "This tool can change or run the project."}${summary}`;
    }

    function renderToolApproval(body, approval) {
      body.querySelector(".tool-approval")?.remove();
      if (!approval || approval.status !== "pending_approval") {
        return;
      }
      const panel = document.createElement("div");
      panel.className = "tool-approval";
      const title = document.createElement("strong");
      title.textContent = approval.tool_kind_label || "Approval required";
      const text = document.createElement("span");
      text.textContent = approval.summary || approval.tool_name || "Tool call";
      const actions = document.createElement("div");
      actions.className = "tool-approval-actions";
      const approve = document.createElement("button");
      approve.type = "button";
      approve.className = "primary-button";
      approve.textContent = "Approve";
      approve.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        approve.disabled = true;
        deny.disabled = true;
        onToolApprovalReview?.(approval.id, "approved");
      });
      const deny = document.createElement("button");
      deny.type = "button";
      deny.className = "secondary-button";
      deny.textContent = "Deny";
      deny.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        approve.disabled = true;
        deny.disabled = true;
        onToolApprovalReview?.(approval.id, "denied");
      });
      actions.append(approve, deny);
      panel.append(title, text, actions);
      body.append(panel);
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
        ALLOWED_URI_REGEXP: /^(?:(?:https?|mailto):|res:\/\/|data:image\/(?:png|jpeg|jpg|gif|webp);base64,)/i,
      });
      target.querySelectorAll("a[href]").forEach((link) => {
        const href = String(link.getAttribute("href") || "");
        if (href.toLowerCase().startsWith("res://")) {
          link.classList.add("project-file-link");
          link.addEventListener("click", (event) => {
            event.preventDefault();
            const reference = projectFileReferenceFromLink(link, href);
            onProjectFileReference?.(reference, {
              source: "markdown",
              element: link,
              href: reference,
            });
          });
          return;
        }
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

    function projectFileReferenceFromLink(link, href) {
      const candidates = [
        href,
        link?.getAttribute?.("href"),
        link?.href,
        link?.textContent,
        link?.getAttribute?.("title"),
      ].map(extractProjectFileReference).filter(Boolean);
      return candidates.find(projectFileReferenceHasLine) || candidates[0] || href;
    }

    function extractProjectFileReference(value) {
      const text = String(value || "").trim();
      const match = text.match(/res:\/\/[^\s)\],;'"`<>]+/i);
      return match ? decodeProjectFileReference(match[0].replace(/[.,;!?]+$/, "")) : "";
    }

    function decodeProjectFileReference(reference) {
      try {
        return decodeURI(reference);
      } catch {
        return reference;
      }
    }

    function projectFileReferenceHasLine(value) {
      return /(?:#L?\d+(?:-L?\d+)?|:L?\d+(?:-L?\d+)?)$/i.test(String(value || ""));
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

    function normalizeContextSnippets(snippets) {
      if (!Array.isArray(snippets)) {
        return [];
      }
      return snippets
        .map((snippet) => {
          const path = String(snippet?.path || "").trim();
          const startLine = Number(snippet?.start_line || snippet?.startLine || 0);
          const endLine = Number(snippet?.end_line || snippet?.endLine || startLine || 0);
          if (!path || !Number.isInteger(startLine) || !Number.isInteger(endLine) || startLine <= 0 || endLine < startLine) {
            return null;
          }
          return {
            path,
            start_line: startLine,
            end_line: endLine,
            text: String(snippet?.text || ""),
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

    function renderContextGrid(contexts, className) {
      const grid = document.createElement("div");
      grid.className = className;
      grid.dataset.count = String(contexts.length);
      for (const snippet of contexts) {
        const figure = document.createElement("figure");
        figure.className = "message-context-card";
        const button = document.createElement("button");
        button.type = "button";
        button.className = "message-context-button";
        button.setAttribute("aria-label", `Preview ${contextLabel(snippet)}`);
        const icon = document.createElement("span");
        icon.className = "context-chip-icon";
        icon.textContent = "{}";
        const label = document.createElement("figcaption");
        const title = document.createElement("strong");
        title.textContent = contextLabel(snippet);
        const path = document.createElement("span");
        path.textContent = snippet.path;
        label.append(title, path);
        button.addEventListener("click", () => openContextSnippetPreview(snippet));
        button.append(icon, label);
        figure.append(button);
        grid.append(figure);
      }
      return grid;
    }

    function contextLabel(snippet) {
      const fileName = String(snippet.path || "").split(/[\\/]/).filter(Boolean).pop() || "file";
      const range = snippet.end_line > snippet.start_line
        ? `${snippet.start_line}-${snippet.end_line}`
        : `${snippet.start_line}`;
      return `${fileName}:${range}`;
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

    function openContextSnippetPreview(snippet) {
      let preview = document.querySelector("[data-context-preview]");
      if (!preview) {
        preview = document.createElement("div");
        preview.className = "context-preview";
        preview.dataset.contextPreview = "";
        preview.hidden = true;
        preview.innerHTML = [
          '<button class="context-preview-backdrop" type="button" aria-label="Close context preview" data-context-preview-close></button>',
          '<section class="context-preview-card" role="dialog" aria-label="Selected context preview">',
          "<header>",
          '<div class="context-preview-title">',
          '<strong data-context-preview-title></strong>',
          '<span data-context-preview-subtitle></span>',
          "</div>",
          '<button class="context-preview-close" type="button" aria-label="Close context preview" data-context-preview-close>×</button>',
          "</header>",
          '<pre data-context-preview-code></pre>',
          "</section>",
        ].join("");
        preview.addEventListener("click", (event) => {
          if (event.target.closest("[data-context-preview-close]")) {
            closeContextSnippetPreview(preview);
          }
        });
        document.addEventListener("keydown", (event) => {
          if (event.key === "Escape" && !preview.hidden) {
            closeContextSnippetPreview(preview);
          }
        });
        document.body.append(preview);
      }

      preview.querySelector("[data-context-preview-title]").textContent = contextLabel(snippet);
      preview.querySelector("[data-context-preview-subtitle]").textContent = String(snippet.path || "");
      const code = preview.querySelector("[data-context-preview-code]");
      if (code) {
        code.textContent = String(snippet.text || "").trim() || "(No selected text)";
      }
      preview.hidden = false;
      preview.querySelector(".context-preview-close")?.focus();
    }

    function closeContextSnippetPreview(preview) {
      preview.hidden = true;
    }

    return {
      addActionsToMessage,
      addAssistantActions,
      appendMessage,
      appendStoredThinking,
      appendSystem,
      beginStream,
      clear,
      clearSystemStatus,
      endStream,
      flashCopied,
      finishActiveThinking,
      flushAssistantRender,
      openImagePreview,
      openContextSnippetPreview,
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
