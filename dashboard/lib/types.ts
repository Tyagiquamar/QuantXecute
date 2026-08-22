export type Side = 'buy' | 'sell';
export type SizeMode = 'notional' | 'base';

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
  connected: boolean;
  bookReady: boolean;
  stale: boolean;
  reconnects: number;
  sequenceGaps: number;
  malformedMessages: number;
  staleRejected: number;
  checksumFailures: number;
  messagesAccepted: number;
  lastMessageAgeMs: number;
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
  mid?: number;
  spreadBps?: number;
  sequence: number;
  health: HealthState;
}
