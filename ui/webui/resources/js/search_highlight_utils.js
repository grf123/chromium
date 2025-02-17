// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('cr.search_highlight_utils', function() {
  /** @type {string} */
  const WRAPPER_CSS_CLASS = 'search-highlight-wrapper';

  /** @type {string} */
  const ORIGINAL_CONTENT_CSS_CLASS = 'search-highlight-original-content';

  /** @type {string} */
  const HIT_CSS_CLASS = 'search-highlight-hit';

  /**
   * Applies the highlight UI (yellow rectangle) around all matches in |node|.
   * @param {!Node} node The text node to be highlighted. |node| ends up
   *     being hidden.
   * @param {!Array<string>} tokens The string tokens after splitting on the
   *     relevant regExp. Even indices hold text that doesn't need highlighting,
   *     odd indices hold the text to be highlighted. For example:
   *     const r = new RegExp('(foo)', 'i');
   *     'barfoobar foo bar'.split(r) => ['bar', 'foo', 'bar ', 'foo', ' bar']
   */
  function highlight(node, tokens) {
    const wrapper = document.createElement('span');
    wrapper.classList.add(WRAPPER_CSS_CLASS);
    // Use existing node as placeholder to determine where to insert the
    // replacement content.
    node.parentNode.replaceChild(wrapper, node);

    // Keep the existing node around for when the highlights are removed. The
    // existing text node might be involved in data-binding and therefore should
    // not be discarded.
    const span = document.createElement('span');
    span.classList.add(ORIGINAL_CONTENT_CSS_CLASS);
    span.style.display = 'none';
    span.appendChild(node);
    wrapper.appendChild(span);

    for (let i = 0; i < tokens.length; ++i) {
      if (i % 2 == 0) {
        wrapper.appendChild(document.createTextNode(tokens[i]));
      } else {
        const hitSpan = document.createElement('span');
        hitSpan.classList.add(HIT_CSS_CLASS);
        hitSpan.style.backgroundColor = '#ffeb3b';  // --var(--paper-yellow-500)
        hitSpan.textContent = tokens[i];
        wrapper.appendChild(hitSpan);
      }
    }
  }

  /**
   * Finds all previous highlighted nodes under |node| (both within self and
   * children's Shadow DOM) and replaces the highlights (yellow rectangles)
   * with the original search node.
   * @param {!Node} node
   * @private
   */
  function findAndRemoveHighlights(node) {
    const wrappers = node.querySelectorAll('* /deep/ .' + WRAPPER_CSS_CLASS);

    for (let i = 0; i < wrappers.length; i++) {
      const wrapper = wrappers[i];
      const originalNode =
          wrapper.querySelector('.' + ORIGINAL_CONTENT_CSS_CLASS);
      wrapper.parentElement.replaceChild(originalNode.firstChild, wrapper);
    }
  }

  return {
    highlight: highlight,
    findAndRemoveHighlights: findAndRemoveHighlights,
  };
});
