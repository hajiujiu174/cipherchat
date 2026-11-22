export type CellRange = 0 | 1 | 2;
export type ValidMove = 0 | 1 | 2 | 3 | 4 | 5 | 6;

export const PLAYER = {
  ONE: 'ONE',
  TWO: 'TWO',
} as const;
export type Player = keyof typeof PLAYER;

export type Coordinate = {
  col: number;
  row: number;
};

export class ConnectFourGameState {
  board: CellRange[] = [];
  playersTurn: Player = PLAYER.ONE;
  isGameOver = false;
  gameWinner: undefined | Player;
  winningCells: Coordinate[] = [];
  moveHistory: ValidMove[] = [];
}

export const CONNECT_FOUR_ERROR = {
  INVALID_MOVE: 'Column is already filled, please provide a different column.',
  INVALID_COLUMN: 'Invalid column specified. Please provide a valid column number.',
  INVALID_MOVE_GAME_IS_OVER: 'Game has already ended, please reset the game.',
} as const;
