// "Learn more & feedback" modal shared by every page with the sidebar footer.
(function () {
    const FEEDBACK_EMAIL = 'feedback@sugarclock.com';
    const VIDEO_ID = 'vfTbYTnHQdA';
    const SITE_HOST = 'sugarclock.com';
    const SITE_URL = 'https://' + SITE_HOST;

    let dialog = null;
    let opener = null;

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
            <div class="help-links">
                <a class="help-link" href="https://www.youtube.com/watch?v=${VIDEO_ID}" target="_blank" rel="noopener">
                    <svg class="nav-icon" viewBox="0 0 20 20" aria-hidden="true"><rect x="2" y="4" width="16" height="12" rx="3"/><path d="m8.5 7.5 4 2.5-4 2.5z"/></svg>
                    <span><strong data-i18n="Watch on YouTube">${h('Watch on YouTube')}</strong><small data-i18n="The SugarClock walkthrough video">${h('The SugarClock walkthrough video')}</small></span>
                </a>
                <a class="help-link" href="${SITE_URL}" target="_blank" rel="noopener">
                    <svg class="nav-icon" viewBox="0 0 20 20" aria-hidden="true"><circle cx="10" cy="10" r="8"/><path d="M2 10h16M10 2c2.5 2.6 2.5 13.4 0 16M10 2c-2.5 2.6-2.5 13.4 0 16"/></svg>
                    <span><strong class="help-site-label"></strong><small data-i18n="Setup guide, FAQ, and updates">${h('Setup guide, FAQ, and updates')}</small></span>
                </a>
                <a class="help-link help-feedback-link" href="${mailto()}">
                    <svg class="nav-icon" viewBox="0 0 20 20" aria-hidden="true"><rect x="2" y="4" width="16" height="12" rx="2"/><path d="m3 5 7 6 7-6"/></svg>
                    <span><strong data-i18n="Send feedback or get help">${h('Send feedback or get help')}</strong><small><bdi>${FEEDBACK_EMAIL}</bdi></small></span>
                </a>
            </div>`;
        document.body.appendChild(dialog);
        I18n.text(dialog.querySelector('.help-site-label'), I18n.message('Visit {0}', [SITE_HOST]));
        document.addEventListener('languagechange', () => {
            dialog.querySelector('.help-feedback-link').href = mailto();
        });

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
            if (opener && opener.isConnected) opener.focus();
        });
        bindFacade();
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

    // Safari does not focus buttons on click, so the footer button passes itself in.
    window.openHelp = function (trigger) {
        if (!dialog) build();
        opener = trigger || document.activeElement;
        dialog.showModal();
    };
})();
