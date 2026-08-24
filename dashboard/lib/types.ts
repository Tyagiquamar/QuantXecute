export type Side = 'buy' | 'sell';
export type SizeMode = 'notional' | 'base';

// WebSocket transport lifecycle, kept independent from REST reachability.
export type TransportState = 'connecting' | 'connected' | 'reconnecting' | 'unavailable';

export interface Level {
  price: number;
  size: number;
}

export interface BookState {
  sequence: number;
  bids: Level[];
  asks: Level[];
  mid?: number;
  spreadBps?: number;
}

export interface HealthState {
  mode?: string;
  exchange?: string;
  instrument?: string;
  channel?: string;

  connected: boolean;
  bookReady: boolean;
  stale: boolean;
  reconnects: number;
  sequenceGaps: number;
  malformedMessages: number;
  staleRejected: number;
  checksumFailures: number;
  messagesAccepted: number;
  lastSeqId?: number;
  lastMessageAgeMs: number;

  // Explicit server-reported integrity mode, e.g. "seqId/prevSeqId".
  // Present on REST /health; absent from WS /events payloads.
  integrity?: string;
}

export interface ExecutionResult {
  side: Side;
  requestedNotional: number;
  requestedBaseQty: number;
  filledNotional: number;
  filledBaseQty: number;
  referenceMid: number;
  bestPrice: number;
  vwap: number;
  spreadBps: number;
  slippageBps: number;
  slippageUsd: number;
  feeBps: number;
  feeUsd: number;
  totalCostBps: number;
  totalCostUsd: number;
  levelsConsumed: number;
  insufficientLiquidity: boolean;
}

export interface EngineEvent {
  mode?: string;
  exchange?: string;
  instrument?: string;
  mid?: number;
  spreadBps?: number;
  sequence: number;
  bookReady?: boolean;
  health: HealthState;
}