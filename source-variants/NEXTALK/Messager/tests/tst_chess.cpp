#include "services/chessrules.h"
#include "services/chesscontroller.h"
#include "services/gomokuwire.h"
#include "services/mockservice.h"
#include "ui/chesspage.h"
#include "ui/miniappspage.h"
#include "ui/theme.h"
#include <QDir>
#include <QPushButton>
#include <QtTest>
using namespace Messager;
class ChessTest:public QObject {
    Q_OBJECT
    static quint64 perft(const QJsonArray& history,int depth){ChessState s;if(!ChessRules::replay("chess",history,s))return 0;if(!depth)return 1;quint64 n=0;for(const auto& code:ChessRules::legalMoves(s)){auto next=history;next.append(code);n+=perft(next,depth-1);}return n;}
private slots:
    void startingPositionsAndPerft() {
        ChessState c;QVERIFY(ChessRules::replay("chess",{},c));QCOMPARE(ChessRules::legalMoves(c).size(),20);QCOMPARE(perft({},2),quint64(400));QCOMPARE(perft({},3),quint64(8902));
        QVERIFY(ChessRules::replay("xiangqi",{},c));QCOMPARE(ChessRules::legalMoves(c).size(),44);QCOMPARE(c.board[85],6);QCOMPARE(c.board[4],-6);
    }
    void chessCastlingAndAttackedTransit() {
        ChessState c;QVERIFY(ChessRules::chessPosition("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",c));auto moves=ChessRules::legalMoves(c);QVERIFY(moves.contains("e1g1")&&moves.contains("e1c1"));
        QVERIFY(ChessRules::chessPosition("r3k2r/8/8/8/2b5/8/8/R3K2R w KQkq - 0 1",c));QVERIFY(!ChessRules::legalMoves(c).contains("e1g1"));
        QVERIFY(ChessRules::replay("chess",{"e2e4","e7e5","g1f3","b8c6","f1c4","g8f6","e1g1"},c));QCOMPARE(c.board[62],6);QCOMPARE(c.board[61],4);QCOMPARE(c.board[60],0);
    }
    void enPassantAndPinnedPawn() {
        ChessState c;QVERIFY(ChessRules::replay("chess",{"e2e4","a7a6","e4e5","d7d5","e5d6"},c));QCOMPARE(c.board[19],1);QCOMPARE(c.board[27],0);
        QVERIFY(ChessRules::chessPosition("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",c));QVERIFY(!ChessRules::legalMoves(c).contains("e5d6"));
    }
    void allPromotionsAndCheckmate() {
        ChessState c;QVERIFY(ChessRules::chessPosition("7k/P7/8/8/8/8/8/7K w - - 0 1",c));auto legal=ChessRules::legalMoves(c);for(auto p:{"q","r","b","n"})QVERIFY(legal.contains(QString("a7a8")+p));
        QVERIFY(ChessRules::replay("chess",{"f2f3","e7e5","g2g4","d8h4"},c));QCOMPARE(c.winner,2);QVERIFY(c.check);
        QVERIFY(!ChessRules::replay("chess",{"f2f3","e7e5","g2g4","d8h4","a2a3"},c));
    }
    void chessDrawsAndMalformedHistory() {
        ChessState c;QVERIFY(ChessRules::chessPosition("7k/5K2/6Q1/8/8/8/8/8 b - - 0 1",c));QCOMPARE(c.winner,3);QVERIFY(!c.check);
        QVERIFY(ChessRules::chessPosition("7k/8/8/8/8/8/8/K7 w - - 0 1",c));QCOMPARE(c.winner,3);
        QVERIFY(ChessRules::replay("chess",{"g1f3","g8f6","f3g1","f6g8","g1f3","g8f6","f3g1","f6g8"},c));QCOMPARE(c.winner,3);
        QVERIFY(!ChessRules::replay("chess",{"e2e5"},c));QVERIFY(!ChessRules::replay("chess",{QJsonArray{1,2}},c));QVERIFY(!ChessRules::replay("chess",{"e2e4q"},c));
    }
    void xiangqiHorseAndElephantBlocks() {
        ChessState c;QVERIFY(ChessRules::replay("xiangqi",{},c));auto moves=ChessRules::legalMoves(c);QVERIFY(moves.contains("b0c2"));QVERIFY(moves.contains("c0e2"));
        auto b=c.board;b[73]=1;c=ChessRules::xiangqiPosition(b,1);QVERIFY(!ChessRules::legalMoves(c).contains("b0c2"));
        b[73]=0;b[75]=1;c=ChessRules::xiangqiPosition(b,1);QVERIFY(!ChessRules::legalMoves(c).contains("c0e2"));
        b.fill(0);b[85]=6;b[4]=-6;b[49]=1;b[47]=3;c=ChessRules::xiangqiPosition(b,1);QVERIFY(!ChessRules::legalMoves(c).contains("c4e6"));
    }
    void xiangqiCannonAndFlyingGenerals() {
        ChessState c;QVERIFY(ChessRules::replay("xiangqi",{},c));auto b=c.board;b.fill(0);b[85]=6;b[4]=-6;b[49]=1;b[64]=7;b[37]=-4;
        c=ChessRules::xiangqiPosition(b,1);QVERIFY(!ChessRules::legalMoves(c).contains("b2b5"));
        b[55]=1;c=ChessRules::xiangqiPosition(b,1);QVERIFY(ChessRules::legalMoves(c).contains("b2b5"));
        b[46]=1;c=ChessRules::xiangqiPosition(b,1);QVERIFY(!ChessRules::legalMoves(c).contains("b2b5"));
        b.fill(0);b[85]=6;b[4]=-6;b[49]=4;c=ChessRules::xiangqiPosition(b,1);QVERIFY(!ChessRules::legalMoves(c).contains("e4d4"));QVERIFY(ChessRules::legalMoves(c).contains("e4e5"));
    }
    void xiangqiPalaceSoldiersAndStalemate() {
        std::array<int,90>b{};b[85]=6;b[4]=-6;b[49]=1;b[40]=1;auto c=ChessRules::xiangqiPosition(b,1);auto moves=ChessRules::legalMoves(c);QVERIFY(moves.contains("e5d5")&&moves.contains("e5e6"));QVERIFY(!moves.contains("e5e4"));
        b[40]=0;b[49]=0;b[58]=1;c=ChessRules::xiangqiPosition(b,1);QVERIFY(!ChessRules::legalMoves(c).contains("e3d3"));QVERIFY(!ChessRules::legalMoves(c).contains("e0f1"));
        // Black king has no move but is not checked: this is a loss in Xiangqi.
        b.fill(0);b[4]=-6;b[85]=6;b[12]=4;b[14]=4;b[40]=1;c=ChessRules::xiangqiPosition(b,2);QVERIFY(!c.check);QCOMPARE(c.winner,1);
    }
    void xiangqiRepetitionAndLegalReplay() {
        ChessState c;QVERIFY(ChessRules::replay("xiangqi",{"b0c2","b9c7","c2b0","c7b9","b0c2","b9c7","c2b0","c7b9"},c));QCOMPARE(c.winner,3);
        QVERIFY(!ChessRules::replay("xiangqi",{"b0b2"},c));QVERIFY(!ChessRules::replay("xiangqi",{"e0e3"},c));QVERIFY(!ChessRules::replay("xiangqi",{"a0z9"},c));
    }
    void standaloneBoardsAndWire() {
        MockService service;MiniAppsPage page(&service);page.show();const auto dir=qEnvironmentVariable("CHESS_EVIDENCE",QDir::tempPath()+"/chess-evidence");QDir().mkpath(dir);
        for(const auto& game:QStringList{"chess","xiangqi"}){
            auto text=GomokuWire::encode({{"game",game},{"type","invite"},{"room",QString(32,'c')},{"v",1}});QCOMPARE(GomokuWire::decode(text)["game"].toString(),game);
            page.openApp(game);auto all=page.findChildren<ChessPage*>();ChessPage* window=nullptr;for(auto* p:all)if(p->controller()->gameId()==game)window=p;
            QVERIFY(window&&window->isFullScreen());auto* board=window->findChild<ChessBoard*>();QVERIFY(board);ChessRoom r;r.game=game;QVERIFY(ChessController::replay(r,{}));board->setRoom(r,true);
            const int x=game=="chess"?4:0,y=6;QTest::mouseClick(board,Qt::LeftButton,{},board->cellCenter(x,y));
            for(auto mode:{Theme::Light,Theme::Dark}){Theme::instance().setMode(mode);QTest::qWait(40);QVERIFY(window->grab().save(dir+"/"+game+(mode==Theme::Light?"-light.png":"-dark.png")));}
            QSignalSpy picked(board,&ChessBoard::moveChosen);QTest::mouseClick(board,Qt::LeftButton,{},board->cellCenter(x,game=="chess"?4:5));QCOMPARE(picked.size(),1);
            const auto unflipped=board->cellCenter(0,0);board->setRoom(r,true,true);QVERIFY(board->cellCenter(0,0)!=unflipped);
            QTest::mouseClick(window->findChild<QPushButton*>("chessFullScreen"),Qt::LeftButton);QVERIFY(!window->isFullScreen());window->close();QVERIFY(page.isVisible());
        }
    }
};
QTEST_MAIN(ChessTest)
#include "tst_chess.moc"
