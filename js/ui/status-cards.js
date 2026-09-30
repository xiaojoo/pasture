// One builder for every board status block in the drawers.
//
// Each drawer used to keep its own copy of `chip()` and lay its readings out as
// pill-shaped label/value pairs, which meant five different visual weights for the
// same kind of information and a block that could not be read at a glance. They are
// now the same cards the 实时状态 grids above them use -- three to a row -- so the
// only thing that differs between the five boards is what they measure.

function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

// The grid itself: `three` is the three-column variant of the drawer's .data-grid.
export function cardGrid() {
  return el('div', 'data-grid three');
}

// A heading for a block the panel builds itself, in the same type the drawer's own
// markup uses, so a panel-mounted block and a hand-written section read alike.
export function sectionLabel(mount, text) {
  const n = el('div', 'section-label', text);
  mount.appendChild(n);
  return n;
}

// One reading. Returns the node the panel keeps a reference to; `value` is what gets
// rewritten on every refresh.
export function cell(grid, label) {
  const card = el('div', 'data-card');
  card.appendChild(el('div', 'data-label', label));
  const value = card.appendChild(el('div', 'data-value', '-'));
  grid.appendChild(card);
  return { card, value };
}

// Write a reading. The unit goes into the card's <small>, exactly like the 实时状态
// cards do, so "225 V" is one number with a suffix rather than a sentence.
// Built from nodes rather than innerHTML: several of these strings arrive off the
// serial line and must not be able to inject markup.
export function put(target, text, unit, cls) {
  const value = target.value || target;
  if (!value) return;
  value.textContent = '';
  value.appendChild(document.createTextNode(String(text)));
  if (unit) value.appendChild(el('small', null, unit));
  value.className = `data-value${cls ? ' ' + cls : ''}`;
}

// The long answers -- a compile progress string, a panel clock -- get a line of
// their own under the grid instead of a card that cannot hold them.
export function statusLine(mount, text) {
  const n = el('div', 'card-status', text);
  mount.appendChild(n);
  return n;
}

// A row the drawer's own markup renders above this panel (the 消防设备 / 电力设备
// lists): its status text becomes the board's answer. Written by id because the
// markup is rebuilt on every drawer open, so a reference taken once goes stale.
export function putRow(id, text, cls) {
  const node = document.getElementById(id);
  if (!node) return;
  node.textContent = text;
  node.className = `device-status${cls ? ' ' + cls : ''}`;
}
