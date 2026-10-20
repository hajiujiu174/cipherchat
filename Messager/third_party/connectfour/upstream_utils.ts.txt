import { CONNECT_FOUR_ERROR, CellRange, ConnectFourGameState, Coordinate, PLAYER, ValidMove } from './data';

const NUMBER_OF_ROWS = 6;
const NUMBER_OF_COLS = 7;

/**
 * Takes the provided Coordinate (col and row) object and converts that into an index
 * in the 1D Array that represents a 2D Array.
 *
 * Example:
 * If we want to represent a 2D Array like:
 * [
 *   [0, 0, 0],
 *   [0, 1, 0],
 *   [0, 0, 0]
 * ]
 * as a 1D Array, it would be: [0,0,0,0,1,0,0,0,0]
 *
 * If we are given index [1][1] in the 2D array, we can convert this into the 1D array
 * index like: [4], since we have 3 columns, we know there are 3 elements in 1 row.
 */
export function getArrayIndexFrom2DCoordinate(coordinate: Coordinate): number {
  return coordinate.row * NUMBER_OF_COLS + coordinate.col;
}

/**
 * Takes the provided index of a 1D Array that represents a 2D Array and converts that
 * index into a Coordinate in the 2D Array.
 *
 * Example:
 * If we want to represent a 2D Array like:
 * [
 *   [0, 0, 0],
 *   [0, 1, 0],
 *   [0, 0, 0]
 * ]
 * as a 1D Array, it would be: [0,0,0,0,1,0,0,0,0]
 *
 * If we are given index 4 in the 1D array, we can convert this into the 2D array
 * index like: [1][1], since we have 3 columns, we know there are 3 elements in 1 row.
 */
export function get2DPosition(index: number): Coordinate {
  const row = Math.floor(index / NUMBER_OF_COLS);
  const col = index % NUMBER_OF_COLS;
  return { row, col };
}

/**
 * These are utility function for calculating the min/max col/row to check for a winning combination
 * when a new game piece is added to the Connect Four board. When a game piece is added, we need to check
 * up to four possible row/col combinations and these functions are used to help determine those values.
 *
 * Note: we have a limit on being 3 cells away since we only win the game if there are 4 game pieces in a row.
 */

export function min(num: number): number {
  return Math.max(num - 3, 0);
}

export function max(num: number, max: number): number {
  return Math.min(num + 3, max);
}

/**
 * Initializes the provided 1D array that represents the Connect Four board with Zeros, which represent empty spaces.
 */
export function generateEmptyBoard(): CellRange[] {
  // reset grid back to an empty state
  const emptyBoard: CellRange[] = [];

  // populate the rows and cols for the grid
  for (let i = 0; i < NUMBER_OF_ROWS; i += 1) {
    for (let j = 0; j < NUMBER_OF_COLS; j += 1) {
      emptyBoard.push(0);
    }
  }

  return emptyBoard;
}

/**
 * Initializes the game state back to the a fresh instance of a Connect Four game, where the board is empty
 * and we are waiting for the first player to make a move.
 */
export function initializeConnectFourGameState(gameState: ConnectFourGameState): void {
  gameState.board = generateEmptyBoard();
  gameState.isGameOver = false;
  gameState.gameWinner = undefined;
  gameState.playersTurn = PLAYER.ONE;
  gameState.winningCells = [];
  gameState.moveHistory = [];
}

/**
 * Checks to see if all cells in the provided array match the provided value. If so,
 * update the internal game state to have the appropriate winner value.
 */
export function doAllCellsMatch(cells: number[], value: number): boolean {
  const isWin = cells.every((cell) => {
    return cell === value && cell !== 0;
  });
  if (isWin) {
    return true;
  }
  return false;
}

/**
 * Checks to see if the last played game piece resulted in four in a row in a backward diagonal.
 * Checks the possible winning combinations for the given row and column based on where the game piece was played.
 *
 * Example:
 * If the piece was played in column 3 row 3, we would need to check the following combinations for a win
 * since column 3 row 3 is included in each combination:
 *  - [{col: 5, row: 5}, {col: 4, row: 4}, {col: 3, row: 3}, {col: 2, row: 2}]
 *  - [{col: 4, row: 4}, {col: 3, row: 3}, {col: 2, row: 2}, {col: 1, row: 1}]
 *  - [{col: 3, row: 3}, {col: 2, row: 2}, {col: 1, row: 1}, {col: 0, row: 0}]
 */
export function isBackwardSlashWin(
  gameState: ConnectFourGameState,
  row: number,
  col: number,
  maxCol: number,
  maxRow: number,
): boolean {
  //  need to determine a new max and min based on the diagonal
  let maxDiagonalSpaces = Math.min(maxCol - col, maxRow - row);
  while (maxDiagonalSpaces >= 0) {
    const tempCol = col + maxDiagonalSpaces;
    const tempRow = row + maxDiagonalSpaces;

    if (tempRow > 2 && tempCol >= 3) {
      const cells: CellRange[] = [
        gameState.board[getArrayIndexFrom2DCoordinate({ row: tempRow, col: tempCol })],
        gameState.board[getArrayIndexFrom2DCoordinate({ row: tempRow - 1, col: tempCol - 1 })],
        gameState.board[getArrayIndexFrom2DCoordinate({ row: tempRow - 2, col: tempCol - 2 })],
        gameState.board[getArrayIndexFrom2DCoordinate({ row: tempRow - 3, col: tempCol - 3 })],
      ];
      const cellValue = gameState.board[getArrayIndexFrom2DCoordinate({ row, col })];
      const isWin = doAllCellsMatch(cells, cellValue);
      if (isWin) {
        gameState.winningCells = [
          { col: tempCol, row: tempRow },
          { col: tempCol - 1, row: tempRow - 1 },
          { col: tempCol - 2, row: tempRow - 2 },
          { col: tempCol - 3, row: tempRow - 3 },
        ];
        updateWinnerInGameState(gameState, cellValue);
        return true;
      }
    }
    maxDiagonalSpaces--;
  }
  return false;
}

/**
 * Checks to see if the last played game piece resulted in four in a row in a forward diagonal.
 * Checks the possible winning combinations for the given row and column based on where the game piece was played.
 *
 * Example:
 * If the piece was played in column 3 row 3, we would need to check the following combinations for a win
 * since column 3 row 3 is included in each combination:
 *  - [{col: 1, row: 5}, {col: 2, row: 4}, {col: 3, row: 3}, {col: 4, row: 2}]
 *  - [{col: 2, row: 4}, {col: 3, row: 3}, {col: 4, row: 2}, {col: 5, row: 1}]
 *  - [{col: 3, row: 4}, {col: 4, row: 2}, {col: 5, row: 1}, {col: 6, row: 0}]
 */
export function isForwardSlashWin(
  gameState: ConnectFourGameState,
  row: number,
  col: number,
  minCol: number,
  maxRow: number,
): boolean {
  //  need to determine a new max and min based on the diagonal
  let maxDiagonalSpaces = Math.min(col - minCol, maxRow - row);
  while (maxDiagonalSpaces >= 0) {
    const tempCol = col - maxDiagonalSpaces;
    const tempRow = row + maxDiagonalSpaces;
    if (tempRow > 2 && tempCol <= 3) {
      const cells = [
        gameState.board[getArrayIndexFrom2DCoordinate({ row: tempRow, col: tempCol })],
        gameState.board[getArrayIndexFrom2DCoordinate({ row: tempRow - 1, col: tempCol + 1 })],
        gameState.board[getArrayIndexFrom2DCoordinate({ row: tempRow - 2, col: tempCol + 2 })],
        gameState.board[getArrayIndexFrom2DCoordinate({ row: tempRow - 3, col: tempCol + 3 })],
      ];
      const cellValue = gameState.board[getArrayIndexFrom2DCoordinate({ row, col })];
      const isWin = doAllCellsMatch(cells, cellValue);
      if (isWin) {
        gameState.winningCells = [
          { col: tempCol, row: tempRow },
          { col: tempCol + 1, row: tempRow - 1 },
          { col: tempCol + 2, row: tempRow - 2 },
          { col: tempCol + 3, row: tempRow - 3 },
        ];
        updateWinnerInGameState(gameState, cellValue);
        return true;
      }
    }
    maxDiagonalSpaces--;
  }
  return false;
}

/**
 * Checks to see if the last played game piece resulted in four in a row in the provided column.
 * When we check for a win in column, we only need to check the current cell and the three cells
 * below that one since there will be no game pieces above the current piece. This means there
 * could only be one possible winning combination for the column.
 *
 * We also only need to check for a win if this is at least the fourth game piece added to this column,
 * since four pieces are required for a win.
 */
export function isVerticalWin(gameState: ConnectFourGameState, row: number, col: number): boolean {
  if (row > 2) return false;
  const cells = [
    gameState.board[getArrayIndexFrom2DCoordinate({ row: row, col: col })],
    gameState.board[getArrayIndexFrom2DCoordinate({ row: row + 1, col: col })],
    gameState.board[getArrayIndexFrom2DCoordinate({ row: row + 2, col: col })],
    gameState.board[getArrayIndexFrom2DCoordinate({ row: row + 3, col: col })],
  ];
  const cellValue = gameState.board[getArrayIndexFrom2DCoordinate({ row, col })];
  const isWin = doAllCellsMatch(cells, cellValue);
  if (isWin) {
    gameState.winningCells = [
      { col, row },
      { col, row: row + 1 },
      { col, row: row + 2 },
      { col, row: row + 3 },
    ];
    updateWinnerInGameState(gameState, cellValue);
  }
  return isWin;
}

/**
 * Checks to see if the last played game piece resulted in four in a row in the provided row.
 * Checks the possible winning combinations for the given row based on where the game piece was played.
 *
 * Example:
 * If the piece was played in column 2, we would need to check the following combinations for a win
 * since column 2 is included in these combinations for a row.
 *  - [0,1,2,3]
 *  - [1,2,3,4]
 *  - [2,3,4,5]
 */
export function isHorizontalWin(
  gameState: ConnectFourGameState,
  lastPiecePlayedRow: number,
  minCol: number,
  maxCol: number,
): boolean {
  const row = lastPiecePlayedRow;
  for (let col = minCol; col <= maxCol - 3; col++) {
    const cells = [
      gameState.board[getArrayIndexFrom2DCoordinate({ row: row, col: col })],
      gameState.board[getArrayIndexFrom2DCoordinate({ row: row, col: col + 1 })],
      gameState.board[getArrayIndexFrom2DCoordinate({ row: row, col: col + 2 })],
      gameState.board[getArrayIndexFrom2DCoordinate({ row: row, col: col + 3 })],
    ];
    const cellValue = gameState.board[getArrayIndexFrom2DCoordinate({ row, col })];
    const isWin = doAllCellsMatch(cells, cellValue);
    if (isWin) {
      gameState.winningCells = [
        { col, row },
        { col: col + 1, row },
        { col: col + 2, row },
        { col: col + 3, row },
      ];
      updateWinnerInGameState(gameState, cellValue);
      return true;
    }
  }
  return false;
}

/**
 * Checks if the last played game piece resulted in a player winning the game by getting four in a row,
 * either in a column, row, or diagonal. If after the last piece is placed and there are no remaining game
 * spots left, the game ends in a draw and the game is considered over.
 */
export function checkForGameEnd(gameState: ConnectFourGameState, row: number, col: number): void {
  const minCol = min(col);
  const maxCol = max(col, NUMBER_OF_COLS - 1);
  const maxRow = max(row, NUMBER_OF_ROWS - 1);

  // see if a player won based off of last piece that placed
  const didPlayerWin =
    isHorizontalWin(gameState, row, minCol, maxCol) ||
    isVerticalWin(gameState, row, col) ||
    isForwardSlashWin(gameState, row, col, minCol, maxRow) ||
    isBackwardSlashWin(gameState, row, col, maxCol, maxRow);

  const anyOpenSpots = gameState.board.some((cell) => {
    return cell === 0;
  });

  if (!anyOpenSpots || didPlayerWin) {
    gameState.isGameOver = true;
  }
}

/**
 * Allows player to reset the game state, and start a brand new game.
 */
export function resetGame(gameState: ConnectFourGameState): void {
  initializeConnectFourGameState(gameState);
}

/**
 * Allows the current player to make a move by providing the column they would like to drop their game piece into.
 */
export function makeMove(gameState: ConnectFourGameState, col: number): Coordinate {
  // check for validate board space
  if (col < 0 || col > NUMBER_OF_COLS) {
    throw new Error(CONNECT_FOUR_ERROR.INVALID_COLUMN);
  }

  // validate that the game is not already done
  if (gameState.isGameOver) {
    throw new Error(CONNECT_FOUR_ERROR.INVALID_MOVE_GAME_IS_OVER);
  }

  // validate that the space is not already taken
  if (gameState.board[getArrayIndexFrom2DCoordinate({ row: 0, col: col })] !== 0) {
    throw new Error(CONNECT_FOUR_ERROR.INVALID_MOVE);
  }

  // update the state of the board to include the players selected space
  let row = 0;
  for (let i = NUMBER_OF_ROWS - 1; i >= 0; i -= 1) {
    const index = getArrayIndexFrom2DCoordinate({ row: i, col: col });
    if (gameState.board[index] === 0) {
      if (gameState.playersTurn === PLAYER.ONE) {
        gameState.board[index] = 1;
      } else {
        gameState.board[index] = 2;
      }
      row = i;
      break;
    }
  }

  // check if the game is finished
  checkForGameEnd(gameState, row, col);

  if (!gameState.isGameOver) {
    // update the current players turn
    if (gameState.playersTurn === PLAYER.ONE) {
      gameState.playersTurn = PLAYER.TWO;
    } else {
      gameState.playersTurn = PLAYER.ONE;
    }
  }

  // update the move history
  gameState.moveHistory.push(col as ValidMove);

  return { col, row };
}

/**
 * Updates the game winner int he provided game state.
 */
export function updateWinnerInGameState(gameState: ConnectFourGameState, winningValue: number): void {
  if (winningValue === 1) {
    gameState.gameWinner = PLAYER.ONE;
    return;
  }
  gameState.gameWinner = PLAYER.TWO;
}
