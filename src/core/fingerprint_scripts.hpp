#pragma once

namespace mustela::fp_scripts {

inline constexpr char WEBGL_BLOCK[] = R"js((() => {
  const native = (target, name, impl) => {
    impl.toString = () => 'function ' + name + '() { [native code] }';
    target[name] = impl;
  };
  const orig = HTMLCanvasElement.prototype.getContext;
  native(HTMLCanvasElement.prototype, 'getContext', function(type, ...rest) {
    const kind = String(type);
    if (kind === 'webgl' || kind === 'experimental-webgl' || kind === 'webgl2') return null;
    return orig.call(this, type, ...rest);
  });
})();)js";

inline constexpr char CANVAS_BLANK[] = R"js((() => {
  const native = (target, name, impl) => {
    impl.toString = () => 'function ' + name + '() { [native code] }';
    target[name] = impl;
  };
  const blank = (canvas) => {
    const copy = document.createElement('canvas');
    copy.width = canvas.width;
    copy.height = canvas.height;
    return copy;
  };
  const origData = HTMLCanvasElement.prototype.toDataURL;
  const origBlob = HTMLCanvasElement.prototype.toBlob;
  const origImage = CanvasRenderingContext2D.prototype.getImageData;
  native(HTMLCanvasElement.prototype, 'toDataURL', function(...args) {
    try {
      return origData.apply(blank(this), args);
    } catch (err) {
      return 'data:,';
    }
  });
  native(HTMLCanvasElement.prototype, 'toBlob', function(callback, ...args) {
    try {
      origBlob.call(blank(this), callback, ...args);
    } catch (err) {
      callback(null);
    }
  });
  native(CanvasRenderingContext2D.prototype, 'getImageData', function(sx, sy, sw, sh) {
    const image = origImage.call(this, sx, sy, sw, sh);
    image.data.fill(0);
    return image;
  });
  if (window.WebGLRenderingContext) {
    const origRead = WebGLRenderingContext.prototype.readPixels;
    native(WebGLRenderingContext.prototype, 'readPixels', function(x, y, w, h, fmt, type, out) {
      origRead.call(this, x, y, w, h, fmt, type, out);
      if (out && typeof out.fill === 'function') out.fill(0);
    });
  }
  if (window.WebGL2RenderingContext) {
    const origRead = WebGL2RenderingContext.prototype.readPixels;
    native(WebGL2RenderingContext.prototype, 'readPixels', function(x, y, w, h, fmt, type, out) {
      origRead.call(this, x, y, w, h, fmt, type, out);
      if (out && typeof out.fill === 'function') out.fill(0);
    });
  }
})();)js";

inline constexpr char REFERRER[] = R"js((() => {
  const install = () => {
    const root = document.documentElement;
    if (!root) return false;
    const meta = document.createElement('meta');
    meta.name = 'referrer';
    meta.content = '__POLICY__';
    meta.setAttribute('data-mustela-referrer', '');
    root.insertBefore(meta, root.firstChild);
    return true;
  };
  if (install()) return;
  const observer = new MutationObserver(() => {
    if (install()) observer.disconnect();
  });
  observer.observe(document, {childList: true, subtree: true});
})();)js";

}
