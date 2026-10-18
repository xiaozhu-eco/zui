// Browser-side L1/L2 collector (TESTPLAN sections 3, 6 and 9).
//
// Runs inside the page through Runtime.evaluate and returns an object with exactly the same
// shape as the engine's trace_export.cpp output, so the differ can join them by `path`.
//
// Why Runtime.evaluate instead of DOMSnapshot: the mapping rule in section 9 is about which
// fields are allowed in the trace, not about the transport. Reading the same field set from
// the same live DOM keeps the two collectors symmetric and avoids DOMSnapshot's anonymous
// node naming, which has no engine counterpart.
//
// The step driver injects `window.__zuiDifftest = {props: [...], breaks: bool, advances: bool}`
// before calling collect().

(function () {
  'use strict';

  var OFFSET_BASIS = 1469598103934665603n;
  var FNV_PRIME = 1099511628211n;
  var MASK64 = (1n << 64n) - 1n;

  function utf8Bytes(text) {
    var out = [];
    for (var i = 0; i < text.length; ++i) {
      var code = text.charCodeAt(i);
      if (code < 0x80) {
        out.push(code);
      } else if (code < 0x800) {
        out.push(0xc0 | (code >> 6), 0x80 | (code & 0x3f));
      } else if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.length) {
        var next = text.charCodeAt(i + 1);
        if (next >= 0xdc00 && next <= 0xdfff) {
          var cp = 0x10000 + ((code - 0xd800) << 10) + (next - 0xdc00);
          out.push(0xf0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3f), 0x80 | ((cp >> 6) & 0x3f),
                   0x80 | (cp & 0x3f));
          ++i;
          continue;
        }
        out.push(0xe0 | (code >> 12), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
      } else {
        out.push(0xe0 | (code >> 12), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
      }
    }
    return out;
  }

  // Must stay byte-identical to zdt::style_digest().
  function digest(entries) {
    var hash = OFFSET_BASIS;
    for (var i = 0; i < entries.length; ++i) {
      var keyBytes = utf8Bytes(entries[i][0]);
      for (var k = 0; k < keyBytes.length; ++k) {
        hash = ((hash ^ BigInt(keyBytes[k])) * FNV_PRIME) & MASK64;
      }
      hash = ((hash ^ 61n) * FNV_PRIME) & MASK64;
      var valueBytes = utf8Bytes(entries[i][1]);
      for (var v = 0; v < valueBytes.length; ++v) {
        hash = ((hash ^ BigInt(valueBytes[v])) * FNV_PRIME) & MASK64;
      }
      hash = ((hash ^ 59n) * FNV_PRIME) & MASK64;
    }
    var hex = hash.toString(16);
    while (hex.length < 16) {
      hex = '0' + hex;
    }
    return hex;
  }

  // Rules from props.json. `0px` collapses to `0`; everything else compares as a string (the
  // differ adds the numeric comparison for length values).
  function normalize(value) {
    if (value === null || value === undefined) {
      return '';
    }
    var text = String(value).toLowerCase().replace(/\s+/g, ' ').trim();
    var zero = /^(-?\d*\.?\d+)px$/.exec(text);
    if (zero && parseFloat(zero[1]) === 0) {
      return '0';
    }
    return text;
  }

  function quantize(value) {
    return Math.round(value * 64) / 64;
  }

  function hasNonWhitespace(node) {
    if (node.nodeType === 3) {
      return /\S/.test(node.nodeValue || '');
    }
    if (node.nodeType !== 1) {
      return false;
    }
    for (var i = 0; i < node.childNodes.length; ++i) {
      if (hasNonWhitespace(node.childNodes[i])) {
        return true;
      }
    }
    return false;
  }

  // Mirrors zdt::is_text_owner(): the element's whole visible text comes from its own text
  // children, so the engine's line summary and these Range rects describe the same string.
  function textOwnerInfo(element) {
    var directText = [];
    var sawText = false;
    for (var i = 0; i < element.childNodes.length; ++i) {
      var child = element.childNodes[i];
      if (child.nodeType === 3) {
        directText.push(child);
        if (/\S/.test(child.nodeValue || '')) {
          sawText = true;
        }
        continue;
      }
      if (child.nodeType === 1 && hasNonWhitespace(child)) {
        return null;
      }
    }
    if (!sawText) {
      return null;
    }
    return directText;
  }

  function lineBreaks(textNodes, lines) {
    // TESTPLAN section 6: the codepoint offset where each line starts, so a wrap difference is
    // a structure mismatch and not something a length tolerance can absorb.
    //
    // Each character is measured with a one-character Range and assigned to the recorded line
    // its rect sits on; `result[l]` is the offset of the first character of line l. Offsets are
    // counted in codepoints, the same unit the engine numbers its glyphs in.
    var result = [];
    for (var line = 0; line < lines.length; ++line) {
      result.push(0);
    }
    var painter = document.createRange();
    var running = 0;
    var current = 0;
    for (var n = 0; n < textNodes.length; ++n) {
      var node = textNodes[n];
      var text = node.nodeValue || '';
      for (var i = 0; i < text.length; ) {
        var cp = text.codePointAt(i);
        var size = cp > 0xffff ? 2 : 1;
        painter.setStart(node, i);
        painter.setEnd(node, i + size);
        var rect = painter.getClientRects()[0];
        if (rect) {
          // Nearest line top, and a tie walks forward. Two columns share a top, so the
          // first match would fold the next column back onto line 0 (`multicol-text`).
          var best = assignLine(rect, lines, current);
          if (best > current) {
            current = best;
            result[current] = running;
          }
        }
        running += 1;
        i += size;
      }
    }
    return result;
  }

  // CSS white-space collapsing. A folded run is one cluster (the engine keeps one space
  // glyph). `getClientRects` still returns a box for every source code point: the survivor
  // carries the space width and the rest are 0, which used to make `inline-text` line 2 report
  // 36 advances against the engine's 32.
  function collapsesSpaces(whiteSpace) {
    return whiteSpace === 'normal' || whiteSpace === 'nowrap' || whiteSpace === 'pre-line';
  }

  function isCollapsibleCp(cp, whiteSpace) {
    if (!collapsesSpaces(whiteSpace)) {
      return false;
    }
    if (cp === 0x20 || cp === 0x09 || cp === 0x3000) {
      return true;
    }
    // `pre-line` keeps segment breaks, so a newline is its own cluster there.
    return (cp === 0x0a || cp === 0x0d || cp === 0x0c) && whiteSpace !== 'pre-line';
  }

  function assignLine(rect, lines, current) {
    var best = current;
    var bestDelta = Infinity;
    if (current >= 0 && current < lines.length) {
      bestDelta = Math.abs(rect.top - lines[current]);
    }
    for (var l = 0; l < lines.length; ++l) {
      var delta = Math.abs(rect.top - lines[l]);
      if (delta + 0.01 < bestDelta) {
        bestDelta = delta;
        best = l;
      } else if (Math.abs(delta - bestDelta) <= 0.01 && l >= current && best < current) {
        best = l;
      }
    }
    return best;
  }

  function lineOfRect(rect, lines, fallback) {
    return assignLine(rect, lines, fallback);
  }

  function clusterAdvances(textNodes, lines) {
    var result = [];
    for (var line = 0; line < lines.length; ++line) {
      result.push([]);
    }
    var painter = document.createRange();
    var current = 0;
    for (var n = 0; n < textNodes.length; ++n) {
      var node = textNodes[n];
      var text = node.nodeValue || '';
      var whiteSpace = 'normal';
      if (node.parentElement) {
        whiteSpace = window.getComputedStyle(node.parentElement).whiteSpace || 'normal';
      }
      var i = 0;
      while (i < text.length) {
        var cp = text.codePointAt(i);
        var size = cp > 0xffff ? 2 : 1;
        if (isCollapsibleCp(cp, whiteSpace)) {
          var sum = 0;
          var have = false;
          var runLine = current;
          while (i < text.length) {
            cp = text.codePointAt(i);
            size = cp > 0xffff ? 2 : 1;
            if (!isCollapsibleCp(cp, whiteSpace)) {
              break;
            }
            painter.setStart(node, i);
            painter.setEnd(node, i + size);
            var collapsed = painter.getClientRects()[0];
            if (collapsed) {
              if (!have) {
                runLine = lineOfRect(collapsed, lines, current);
              }
              have = true;
              sum += collapsed.width;
            }
            i += size;
          }
          if (have) {
            current = runLine;
            result[current].push(quantize(sum));
          }
          continue;
        }
        painter.setStart(node, i);
        painter.setEnd(node, i + size);
        var rect = painter.getClientRects()[0];
        if (rect) {
          current = lineOfRect(rect, lines, current);
          result[current].push(quantize(rect.width));
        }
        i += size;
      }
    }
    return result;
  }

  function styleFor(element, props, entries) {
    var computed = window.getComputedStyle(element);
    var out = {};
    for (var i = 0; i < props.length; ++i) {
      var value = normalize(computed.getPropertyValue(props[i]));
      entries.push([props[i], value]);
      out[props[i]] = value;
    }
    return out;
  }

  var pathOf = null;

  function makesContainingBlock(style, fixed) {
    var makes = style.transform !== 'none' || style.filter !== 'none' ||
      (style.backdropFilter && style.backdropFilter !== 'none') ||
      (style.contain && style.contain !== 'none') ||
      (style.containerType && style.containerType !== 'normal');
    if (fixed) {
      return makes;
    }
    return style.position !== 'static' || makes;
  }

  function containingBlockPath(element) {
    var fixed = window.getComputedStyle(element).position === 'fixed';
    var node = element.parentElement;
    while (node) {
      if (makesContainingBlock(window.getComputedStyle(node), fixed)) {
        if (node === element.parentElement) {
          return undefined;
        }
        return pathOf && pathOf.has(node) ? pathOf.get(node) : null;
      }
      node = node.parentElement;
    }
    return null;
  }

  function collectElement(element, path, config, out) {
    if (pathOf) {
      pathOf.set(element, path);
    }
    var rect = element.getBoundingClientRect();
    var display = window.getComputedStyle(element).display;
    var transformed = window.getComputedStyle(element).transform !== 'none';
    var hasBox = display !== 'none' && display !== 'contents';

    var record = {
      path: path,
      tag: element.tagName.toLowerCase(),
      id: element.id || '',
      classes: element.className && typeof element.className === 'string' ? element.className : '',
      has_box: hasBox,
      transformed: transformed
    };

    if (hasBox) {
      record.border_box = [quantize(rect.left), quantize(rect.top), quantize(rect.width),
                           quantize(rect.height)];
      // Chrome exposes scrollWidth/scrollHeight as integers; the differ rounds the engine's
      // values the same way before comparing (section 9: tolerance absorbs representation
      // rounding, never topology).
      record.scroll_size = [element.scrollWidth, element.scrollHeight];
      record.scroll_offset = [quantize(element.scrollLeft), quantize(element.scrollTop)];
      var cs = window.getComputedStyle(element);
      var bl = parseFloat(cs.borderLeftWidth) || 0;
      var bt = parseFloat(cs.borderTopWidth) || 0;
      record.client_box = [quantize(rect.left + bl), quantize(rect.top + bt),
                           quantize(element.clientWidth), quantize(element.clientHeight)];
      var domRects = element.getClientRects();
      record.client_rects = [];
      for (var r = 0; r < domRects.length; ++r) {
        record.client_rects.push([quantize(domRects[r].left), quantize(domRects[r].top),
                                  quantize(domRects[r].width), quantize(domRects[r].height)]);
      }
      record.scroll_rects = [];
      if (element.scrollWidth > element.clientWidth + 0.5 ||
          element.scrollHeight > element.clientHeight + 0.5) {
        record.scroll_rects.push([0, 0, element.scrollWidth, element.scrollHeight]);
      }
      record.fragmented = domRects.length > 1;
      record.out_of_flow = cs.position === 'absolute' || cs.position === 'fixed';
      if (record.out_of_flow) {
        var cbPath = containingBlockPath(element);
        if (cbPath !== undefined) {
          record.containing_block = cbPath;
        }
      }
    } else {
      record.border_box = [0, 0, 0, 0];
      record.client_box = [0, 0, 0, 0];
      record.client_rects = [];
      record.scroll_rects = [];
      record.scroll_size = [0, 0];
      record.scroll_offset = [0, 0];
      record.fragmented = false;
      record.out_of_flow = false;
    }

    var entries = [];
    record.style = styleFor(element, config.props, entries);
    record.style_digest = digest(entries);

    if (config.lines && hasBox) {
      var textNodes = textOwnerInfo(element);
      if (textNodes !== null) {
        var range = document.createRange();
        range.setStartBefore(textNodes[0]);
        range.setEndAfter(textNodes[textNodes.length - 1]);
        var boxes = range.getClientRects();
        if (boxes.length > 0) {
          var tops = [];
          var heights = [];
          for (var i = 0; i < boxes.length; ++i) {
            var top = quantize(boxes[i].top);
            // text-overflow:ellipsis on one nowrap line returns two client rects with the same
            // top. That is one line. A later column can share a top with an earlier line, but
            // that rect does not follow a rect of the same top.
            if (tops.length > 0 && top === tops[tops.length - 1]) {
              continue;
            }
            tops.push(top);
            heights.push(quantize(boxes[i].height));
          }
          var style = window.getComputedStyle(element);
          var borderTop = parseFloat(style.borderTopWidth) || 0;
          var paddingTop = parseFloat(style.paddingTop) || 0;
          var contentTop = rect.top + borderTop + paddingTop;
          var lines = {
            count: tops.length,
            line_count: tops.length,
            tops: tops,
            heights: heights,
            first_line_offset: quantize(tops[0] - contentTop),
            baselines: [],
            baseline_defined: true,
            breaks: config.breaks ? lineBreaks(textNodes, tops) : [],
            advances: []
          };
          lines.line_breaks = lines.breaks;
          if (config.advances) {
            lines.advances = clusterAdvances(textNodes, tops);
          }
          lines.cluster_advances = lines.advances;
          // Canvas ascent is a different quantity from the engine line baseline. The structure
          // field is whether a baseline exists; the numbers stay in the trace for diagnosis.
          record.lines = lines;
        }
      }
    }

    out.push(record);

    var index = 0;
    for (var c = 0; c < element.children.length; ++c) {
      collectElement(element.children[c], path + '/' + index, config, out);
      ++index;
    }
  }

  function pathFromRoot(element) {
    if (!element || element.nodeType !== 1) {
      return null;
    }
    var chain = [];
    var node = element;
    while (node && node !== document.documentElement) {
      chain.push(node);
      node = node.parentElement;
    }
    var path = '0';
    var cursor = document.documentElement;
    for (var i = chain.length - 1; i >= 0; --i) {
      var index = 0;
      var found = false;
      for (var c = 0; c < cursor.children.length; ++c) {
        if (cursor.children[c] === chain[i]) {
          found = true;
          break;
        }
        index += 1;
      }
      if (!found) {
        return null;
      }
      path += '/' + index;
      cursor = chain[i];
    }
    return path;
  }

  function readback(spec) {
    spec = spec || {};
    var target = spec.target || 'window';
    var el = document.documentElement;
    if (target && target !== 'window' && target !== ':root') {
      el = target.charAt(0) === '#' ? document.getElementById(target.slice(1))
                                    : document.querySelector(target);
    }
    var hits = [];
    var listed = spec.hits || [];
    for (var i = 0; i < listed.length; ++i) {
      var node = document.elementFromPoint(listed[i].x, listed[i].y);
      hits.push({x: listed[i].x, y: listed[i].y, path: pathFromRoot(node)});
    }
    var checked = [];
    var boxes = document.querySelectorAll('input');
    for (var b = 0; b < boxes.length; ++b) {
      var type = (boxes[b].getAttribute('type') || 'text').toLowerCase();
      if (type !== 'checkbox' && type !== 'radio') {
        continue;
      }
      checked.push({path: pathFromRoot(boxes[b]), checked: !!boxes[b].checked});
    }
    var scrollers = [];
    function walkScrollers(element) {
      if (!element || element.nodeType !== 1) {
        return;
      }
      var cs = window.getComputedStyle(element);
      // Only fields both ends can see (TESTPLAN section 9). A `display: none` subtree has no box
      // in the engine, so its scroll containers must not be listed here; `display: contents`
      // generates no box of its own either, so it is skipped but its children are not.
      if (cs.display === 'none') {
        return;
      }
      var ox = cs.overflowX;
      var oy = cs.overflowY;
      if (cs.display !== 'contents' &&
          (ox === 'auto' || ox === 'scroll' || oy === 'auto' || oy === 'scroll')) {
        scrollers.push({
          path: pathFromRoot(element),
          x: quantize(element.scrollLeft),
          y: quantize(element.scrollTop)
        });
      }
      for (var c = 0; c < element.children.length; ++c) {
        walkScrollers(element.children[c]);
      }
    }
    walkScrollers(document.documentElement);
    var focus = null;
    if (document.activeElement && document.activeElement !== document.body &&
        document.activeElement !== document.documentElement) {
      focus = pathFromRoot(document.activeElement);
    }
    return {
      schema: 'zui.difftest.state/1',
      op: spec.op || '',
      target: el ? pathFromRoot(el) : null,
      scroll: el ? [quantize(el.scrollLeft), quantize(el.scrollTop)] : [0, 0],
      client_box: el ? [quantize(el.clientWidth), quantize(el.clientHeight)] : [0, 0],
      hit: hits.length ? hits[hits.length - 1].path : null,
      hits: hits,
      scrollers: scrollers,
      focus: focus,
      checked: checked
    };
  }

  function collect() {
    pathOf = new WeakMap();
    var config = window.__zuiDifftest || {};
    var props = config.props || [];
    if (!config.lines) {
      config.lines = true;
    }
    var elements = [];
    collectElement(document.documentElement, '0', config, elements);
    return {
      schema: 'zui.difftest.trace/2',
      engine: config.engine || 'chromium',
      engine_version: config.engineVersion || '',
      viewport: [window.innerWidth, window.innerHeight],
      scroll: [document.documentElement.scrollLeft, document.documentElement.scrollTop],
      elements: elements,
      extras: window.__zuiDifftestExtras || null
    };
  }

  // Defined, not invoked: the driver installs this at document start (before the DOM exists)
  // and calls it from the `trace` step.
  window.__zuiDifftestCollect = collect;
  window.__zuiDifftestReadback = readback;
})();
