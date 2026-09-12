mermaid.initialize({ startOnLoad: false, theme: 'dark', securityLevel: 'strict', suppressErrorRendering: true });
const diagram = document.getElementById('diagram');
const error = document.getElementById('error');
const source = document.getElementById('source');
new ResizeObserver(() => window.chrome.webview.postMessage({ height: document.body.scrollHeight + 24 })).observe(document.body);
window.chrome.webview.addEventListener('message', async event => {
    diagram.replaceChildren();
    error.textContent = '';
    source.hidden = true;
    try {
        const result = await mermaid.render('mermaid-diagram', event.data);
        diagram.innerHTML = result.svg;
    } catch (failure) {
        error.textContent = 'Could not render Mermaid diagram: ' + (failure.message || String(failure));
        source.textContent = event.data;
        source.hidden = false;
    }
});
