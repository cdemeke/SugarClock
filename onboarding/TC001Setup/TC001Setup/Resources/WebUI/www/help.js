// "Learn more & feedback" modal shared by every page with the sidebar footer.
(function () {
    const FEEDBACK_EMAIL = 'feedback@sugarclock.com';
    const VIDEO_ID = 'vfTbYTnHQdA';
    const SITE_HOST = 'sugarclock.com';
    const SITE_URL = 'https://' + SITE_HOST;
    const CHANNEL_URL = 'https://www.youtube.com/@ChrisDemeke/videos';

    let dialog = null;
    let opener = null;
    let statusTimer = 0;

    const t = source => I18n.t(source);
    // Translations are inserted as markup below, so escape them first.
    const h = source => t(source).replace(/[&<>"]/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'})[c]);
    // Translated copy carries data-i18n* so i18n.js re-translates it on language change.
    const facadeHtml = () => `<button type="button" class="help-video-facade" aria-label="${h('Play the SugarClock video')}" data-i18n-aria-label="Play the SugarClock video">
        <img src="https://img.youtube.com/vi/${VIDEO_ID}/hqdefault.jpg" alt="${h('SugarClock video')}" data-i18n-alt="SugarClock video" loading="lazy">
        <span class="help-video-play" aria-hidden="true"></span></button>`;

    function mailto() {
        return 'mailto:' + FEEDBACK_EMAIL + '?subject=' + encodeURIComponent(t('SugarClock feedback'));
    }

    function build() {
        dialog = document.createElement('dialog');
        dialog.className = 'help-dialog';
        dialog.setAttribute('aria-labelledby', 'help-dialog-title');
        dialog.innerHTML = `
            <div class="help-dialog-header">
                <h2 id="help-dialog-title" data-i18n="Learn more &amp; feedback">${h('Learn more & feedback')}</h2>
                <button type="button" class="help-dialog-close" aria-label="${h('Close')}" data-i18n-aria-label="Close">&times;</button>
            </div>
            <div class="help-video">${facadeHtml()}</div>
            <p class="help-dialog-copy" data-i18n="See how SugarClock works, how to set it up, and what it can show.">${h('See how SugarClock works, how to set it up, and what it can show.')}</p>
            <div class="help-actions">
                <a class="help-action" href="https://www.youtube.com/watch?v=${VIDEO_ID}" target="_blank" rel="noopener">
                    <svg class="nav-icon" viewBox="0 0 20 20" aria-hidden="true"><rect x="2" y="4" width="16" height="12" rx="3"/><path d="m8.5 7.5 4 2.5-4 2.5z"/></svg>
                    <span data-i18n="Watch on YouTube">${h('Watch on YouTube')}</span>
                </a>
                <a class="help-action" href="${CHANNEL_URL}" target="_blank" rel="noopener">
                    <svg class="nav-icon" viewBox="0 0 20 20" aria-hidden="true"><rect x="2" y="3" width="7" height="6" rx="1.5"/><rect x="11" y="3" width="7" height="6" rx="1.5"/><rect x="2" y="11" width="7" height="6" rx="1.5"/><rect x="11" y="11" width="7" height="6" rx="1.5"/></svg>
                    <span data-i18n="More videos">${h('More videos')}</span>
                </a>
            </div>
            <section class="help-feedback" aria-labelledby="help-feedback-title">
                <h3 id="help-feedback-title" data-i18n="Feedback &amp; help">${h('Feedback & help')}</h3>
                <p data-i18n="Found a bug or have an idea? Send us an email.">${h('Found a bug or have an idea? Send us an email.')}</p>
                <div class="help-email">
                    <input class="help-email-address" type="email" value="${FEEDBACK_EMAIL}" readonly aria-label="${h('Feedback email address')}" data-i18n-aria-label="Feedback email address">
                    <button type="button" class="help-email-copy">
                        <svg class="nav-icon" viewBox="0 0 20 20" aria-hidden="true"><rect x="7" y="7" width="10" height="10" rx="2"/><path d="M13 7V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v6a2 2 0 0 0 2 2h2"/></svg>
                        <span data-i18n="Copy">${h('Copy')}</span>
                    </button>
                </div>
                <p class="help-email-status" role="status"></p>
                <a class="help-email-open" href="${mailto()}">
                    <svg class="nav-icon" viewBox="0 0 20 20" aria-hidden="true"><rect x="2" y="4" width="16" height="12" rx="2"/><path d="m3 5 7 6 7-6"/></svg>
                    <span data-i18n="Open email app">${h('Open email app')}</span>
                </a>
            </section>
            <p class="help-site"><a href="${SITE_URL}" target="_blank" rel="noopener"></a></p>`;
        document.body.appendChild(dialog);
        I18n.text(dialog.querySelector('.help-site a'), I18n.message('Setup guide, FAQ, and updates at {0}', [SITE_HOST]));
        document.addEventListener('languagechange', () => {
            dialog.querySelector('.help-email-open').href = mailto();
        });
        dialog.querySelector('.help-email-copy').addEventListener('click', copyEmail);

        dialog.querySelector('.help-dialog-close').addEventListener('click', () => dialog.close());
        // Clicking the backdrop (outside the panel, not its padding) closes the modal.
        dialog.addEventListener('click', (e) => {
            if (e.target !== dialog) return;
            const r = dialog.getBoundingClientRect();
            if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) dialog.close();
        });
        // Esc closes natively; on any close stop playback and return focus to the button.
        dialog.addEventListener('close', () => {
            resetVideo();
            setStatus('', false);
            if (opener && opener.isConnected) opener.focus();
        });
        bindFacade();
    }

    function setStatus(source, isError) {
        const status = dialog.querySelector('.help-email-status');
        clearTimeout(statusTimer);
        status.classList.toggle('is-error', isError);
        I18n.text(status, source, Boolean(source));
        if (source && !isError) statusTimer = setTimeout(() => setStatus('', false), 3000);
    }

    // The device UI is served over plain http, where navigator.clipboard is unavailable,
    // so fall back to copying the selected field.
    async function copyEmail() {
        const field = dialog.querySelector('.help-email-address');
        let copied = false;
        if (navigator.clipboard && window.isSecureContext) {
            try { await navigator.clipboard.writeText(FEEDBACK_EMAIL); copied = true; } catch (_) {}
        }
        if (!copied) {
            field.focus();
            field.select();
            try { copied = document.execCommand('copy'); } catch (_) {}
        }
        if (copied) {
            dialog.querySelector('.help-email-copy').focus();
            setStatus('Copied to clipboard.', false);
        } else {
            field.focus();
            field.select();
            setStatus("Couldn't copy. The address is selected so you can copy it yourself.", true);
        }
    }

    function bindFacade() {
        const facade = dialog.querySelector('.help-video-facade');
        facade.addEventListener('click', () => {
            const iframe = document.createElement('iframe');
            iframe.className = 'help-video-embed';
            iframe.src = 'https://www.youtube-nocookie.com/embed/' + VIDEO_ID + '?autoplay=1&rel=0';
            iframe.title = t('SugarClock video');
            iframe.dataset.i18nTitle = 'SugarClock video';
            iframe.allow = 'autoplay; encrypted-media; picture-in-picture';
            iframe.allowFullscreen = true;
            facade.replaceWith(iframe);
        });
    }

    function resetVideo() {
        const iframe = dialog.querySelector('.help-video-embed');
        if (!iframe) return;
        iframe.insertAdjacentHTML('afterend', facadeHtml());
        iframe.remove();
        bindFacade();
    }

    // First-visit hint: a non-modal callout pointing at the footer button until it is
    // dismissed or help is opened. On phones the menu button gets a dot instead.
    const HINT_KEY = 'sugarclock.helpHintDismissed';
    let hint = null;
    let hintTimer = 0;
    let hintRetired = false;

    function hintDismissed() {
        try { return localStorage.getItem(HINT_KEY) === '1'; } catch (_) { return false; }
    }

    function dismissHint() {
        // Opening help before the hint appears also counts, so cancel the pending show too.
        clearTimeout(hintTimer);
        hintRetired = true;
        try { localStorage.setItem(HINT_KEY, '1'); } catch (_) {}
        if (!hint) return;
        const toggle = document.querySelector('.help-toggle');
        toggle.removeAttribute('aria-describedby');
        toggle.classList.remove('has-hint');
        document.querySelector('.mobile-nav-toggle')?.classList.remove('has-hint');
        hint.remove();
        hint = null;
    }

    function placeHint() {
        if (!hint) return;
        const toggle = document.querySelector('.help-toggle');
        const footer = toggle.closest('.sidebar-footer').getBoundingClientRect();
        const button = toggle.getBoundingClientRect();
        hint.style.setProperty('--hint-bottom', (footer.bottom - button.bottom) + 'px');
        hint.style.setProperty('--hint-button-height', button.height + 'px');
    }

    function showHint() {
        const toggle = document.querySelector('.help-toggle');
        if (!toggle || hintRetired || hintDismissed()) return;
        hint = document.createElement('div');
        hint.className = 'help-hint';
        hint.id = 'help-hint';
        hint.setAttribute('role', 'note');
        hint.innerHTML = `
            <p><strong data-i18n="New here?">${h('New here?')}</strong> <span data-i18n="Watch the setup video or send us feedback anytime from here.">${h('Watch the setup video or send us feedback anytime from here.')}</span></p>
            <div class="help-hint-actions">
                <button type="button" class="help-hint-open" data-i18n="Show me">${h('Show me')}</button>
                <button type="button" class="help-hint-dismiss" data-i18n="Got it">${h('Got it')}</button>
            </div>`;
        toggle.before(hint);
        toggle.setAttribute('aria-describedby', 'help-hint');
        toggle.classList.add('has-hint');
        document.querySelector('.mobile-nav-toggle')?.classList.add('has-hint');
        hint.querySelector('.help-hint-open').addEventListener('click', () => openHelp(toggle));
        const close = () => { dismissHint(); toggle.focus(); };
        hint.querySelector('.help-hint-dismiss').addEventListener('click', close);
        hint.addEventListener('keydown', (e) => { if (e.key === 'Escape') close(); });
        window.addEventListener('resize', placeHint);
        document.addEventListener('languagechange', placeHint);
        placeHint();
        requestAnimationFrame(() => hint?.classList.add('is-visible'));
    }

    // Safari does not focus buttons on click, so the footer button passes itself in.
    window.openHelp = function (trigger) {
        dismissHint();
        if (!dialog) build();
        opener = trigger || document.activeElement;
        dialog.showModal();
    };

    hintTimer = setTimeout(showHint, 800);
})();
