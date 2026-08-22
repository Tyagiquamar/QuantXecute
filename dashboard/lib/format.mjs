export function formatUsd(value) {
  if (!Number.isFinite(value)) {
    return '-';
  }
  return `$${value.toLocaleString('en-US', { maximumFractionDigits: 2, minimumFractionDigits: 2 })}`;
}

export function formatBps(value) {
  if (!Number.isFinite(value)) {
    return '-';
  }
  return `${value.toFixed(2)} bps`;
}

export function formatPrice(value) {
  if (!Number.isFinite(value)) {
    return '-';
  }
  return value.toLocaleString('en-US', { minimumFractionDigits: 1, maximumFractionDigits: 2 });
}

export function buildLadder(book, rowsPerSide = 12) {
  const bids = book.bids.slice(0, rowsPerSide).map((level) => ({ ...level, side: 'bid' }));
  const asks = book.asks
    .slice(0, rowsPerSide)
    .map((level) => ({ ...level, side: 'ask' }))
    .reverse();
  return [...asks, ...bids];
}
