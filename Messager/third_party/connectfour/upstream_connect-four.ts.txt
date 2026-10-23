import { CellRange, ConnectFourGameState, Coordinate, Player, ValidMove } from './data';
import { initializeConnectFourGameState, makeMove, resetGame } from './utils';

export class ConnectFour {
  #gameState: ConnectFourGameState;

  constructor() {
    this.#gameState = new ConnectFourGameState();
    initializeConnectFourGameState(this.#gameState);
  }

  get board(): CellRange[] {
    return JSON.parse(JSON.stringify(this.#gameState.board)) as CellRange[];
  }

  get playersTurn(): Player {
    return this.#gameState.playersTurn;
  }

  get isGameOver(): boolean {
    return this.#gameState.isGameOver;
  }

  get gameWinner(): undefined | Player {
    return this.#gameState.gameWinner;
  }

  get winningCells(): Coordinate[] {
    return JSON.parse(JSON.stringify(this.#gameState.winningCells)) as Coordinate[];
  }

  get moveHistory(): ValidMove[] {
    return JSON.parse(JSON.stringify(this.#gameState.moveHistory)) as ValidMove[];
  }

  public resetGame(): void {
    resetGame(this.#gameState);
  }

  public makeMove(col: number): Coordinate {
    return makeMove(this.#gameState, col);
  }
}
