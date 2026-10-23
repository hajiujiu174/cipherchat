#include "chessrules.h"
#include "../../third_party/chess-library/chess.hpp"
#include <QHash>
#include <algorithm>
#include <cmath>
namespace Messager {
namespace {
int owner(int p){return p>0?1:p<0?2:0;}
bool validCode(const QString& game,const QString& m) {
    if(m.size()!=4&&(game!="chess"||m.size()!=5))return false;
    const int cols=game=="xiangqi"?9:8,rows=game=="xiangqi"?10:8;
    for(int n:{0,2})if(m[n]<'a'||m[n]>=QChar('a'+cols)||m[n+1]<(game=="xiangqi"?'0':'1')||m[n+1]>=QChar((game=="xiangqi"?'0':'1')+rows))return false;
    return m.size()==4||QString("qrbn").contains(m[4]);
}
void fill(chess::Board& b,ChessState& s) {
    s.game="chess";s.board.fill(0);s.fen=QString::fromStdString(b.getFen());s.turn=b.sideToMove()==chess::Color::WHITE?1:2;s.check=b.inCheck();
    for(int i=0;i<64;++i){auto p=b.at(chess::Square(i));if(p!=chess::Piece::NONE)s.board[(7-i/8)*8+i%8]=(int(p.type())+1)*(p.color()==chess::Color::WHITE?1:-1);}
    chess::Movelist legal;chess::movegen::legalmoves(legal,b);
    if(legal.empty()){s.winner=s.check?3-s.turn:3;s.reason=s.check?"将死":"无子可动，和棋";}
    else if(b.isInsufficientMaterial()){s.winner=3;s.reason="子力不足，和棋";}
    // Casual-room policy: claimable draws are awarded automatically to both players.
    else if(b.isHalfMoveDraw()){s.winner=3;s.reason="连续 50 回合未吃子或动兵，和棋";}
    else if(b.isRepetition()){s.winner=3;s.reason="三次重复局面，和棋";}
}
bool palace(int x,int y,int side){return x>=3&&x<=5&&(side==1?y>=7&&y<=9:y>=0&&y<=2);}
bool pseudo(const std::array<int,90>& b,int a,int z) {
    if(a<0||a>=90||z<0||z>=90||a==z||!b[a]||owner(b[a])==owner(b[z]))return false;
    int side=owner(b[a]),x=a%9,y=a/9,tx=z%9,ty=z/9,dx=tx-x,dy=ty-y,ax=std::abs(dx),ay=std::abs(dy),between=0;
    if(dx==0||dy==0){int step=dx==0?(dy>0?9:-9):(dx>0?1:-1);for(int k=a+step;k!=z;k+=step)if(b[k])++between;}
    switch(std::abs(b[a])){
    case 1:return (dx==0&&dy==(side==1?-1:1))||((side==1?y<=4:y>=5)&&ay==0&&ax==1);
    case 2:if(ax==2&&ay==1)return !b[y*9+x+dx/2];if(ax==1&&ay==2)return !b[(y+dy/2)*9+x];return false;
    case 3:return ax==2&&ay==2&&(side==1?ty>=5:ty<=4)&&!b[(y+dy/2)*9+x+dx/2];
    case 4:return (dx==0||dy==0)&&between==0;
    case 5:return ax==1&&ay==1&&palace(tx,ty,side);
    case 6:if(std::abs(b[z])==6&&dx==0&&between==0)return true;return ax+ay==1&&palace(tx,ty,side);
    case 7:return (dx==0||dy==0)&&between==(b[z]?1:0);
    }return false;
}
bool attacked(const std::array<int,90>& b,int side) {
    int king=-1;for(int i=0;i<90;++i)if(b[i]==(side==1?6:-6)){king=i;break;}
    if(king<0)return true;
    for(int i=0;i<90;++i)if(owner(b[i])==3-side&&pseudo(b,i,king))return true;
    return false;
}
QStringList xlegal(const std::array<int,90>& b,int side) {
    QStringList moves;for(int a=0;a<90;++a)if(owner(b[a])==side)for(int z=0;z<90;++z)if(pseudo(b,a,z)){
        auto next=b;next[z]=next[a];next[a]=0;if(!attacked(next,side))moves.append(ChessRules::encode("xiangqi",a,z));
    }return moves;
}
QString key(const ChessState& s){QString k=QString::number(s.turn)+":";for(int p:s.board)k+=QChar('A'+p+7);return k;}
}
QString ChessRules::encode(const QString& game,int a,int z,const QString& promotion) {
    const int cols=game=="xiangqi"?9:8,rows=game=="xiangqi"?10:8;
    if(a<0||z<0||a>=cols*rows||z>=cols*rows)return {};
    auto square=[&](int p){return QString(QChar('a'+p%cols))+QChar((game=="xiangqi"?'0':'1')+rows-1-p/cols);};
    return square(a)+square(z)+(game=="chess"?promotion:QString());
}
int ChessRules::from(const QString& game,const QString& m){if(!validCode(game,m))return -1;const int c=game=="xiangqi"?9:8,r=game=="xiangqi"?10:8;return (r-1-(m[1].unicode()-(game=="xiangqi"?'0':'1')))*c+m[0].unicode()-'a';}
int ChessRules::to(const QString& game,const QString& m){if(!validCode(game,m))return -1;return from(game,m.mid(2,2)+m.left(2));}
bool ChessRules::chessPosition(const QString& fen,ChessState& result){chess::Board b;if(!b.setFen(fen.toStdString()))return false;ChessState s;fill(b,s);result=s;return true;}
ChessState ChessRules::xiangqiPosition(const std::array<int,90>& b,int turn){ChessState s;s.game="xiangqi";s.board=b;s.turn=turn;s.check=attacked(b,turn);if(xlegal(b,turn).isEmpty()){s.winner=3-turn;s.reason=s.check?"将死":"无子可动，困毙判负";}return s;}
QStringList ChessRules::legalMoves(const ChessState& s) {
    if(s.winner)return {};
    if(s.game=="xiangqi")return xlegal(s.board,s.turn);
    if(s.game!="chess")return {};
    chess::Board b;if(!b.setFen(s.fen.toStdString()))return {};chess::Movelist moves;chess::movegen::legalmoves(moves,b);QStringList out;for(auto m:moves)out.append(QString::fromStdString(chess::uci::moveToUci(m)));return out;
}
bool ChessRules::replay(const QString& game,const QJsonArray& moves,ChessState& result) {
    if((game!="chess"&&game!="xiangqi")||moves.size()>1024)return false;
    ChessState s;s.game=game;chess::Board board;
    if(game=="chess")fill(board,s);
    else {
        const int back[9]={4,2,3,5,6,5,3,2,4};for(int x=0;x<9;++x){s.board[x]=-back[x];s.board[81+x]=back[x];}
        s.board[19]=s.board[25]=-7;s.board[64]=s.board[70]=7;
        for(int x=0;x<9;x+=2){s.board[27+x]=-1;s.board[54+x]=1;}
    }
    QHash<QString,QVector<int>> positions;positions[key(s)].append(0);QVector<int> checks;int quiet=0;
    for(const auto& v:moves) {
        if(!v.isString()||!validCode(game,v.toString())||s.winner)return false;const auto code=v.toString();
        if(game=="chess"){
            chess::Movelist legal;chess::movegen::legalmoves(legal,board);bool found=false;
            for(auto m:legal)if(QString::fromStdString(chess::uci::moveToUci(m))==code){board.makeMove(m);found=true;break;}
            if(!found)return false;fill(board,s);
        }else{
            const auto legal=xlegal(s.board,s.turn);if(!legal.contains(code))return false;
            const int a=from(game,code),z=to(game,code),side=s.turn;
            quiet=s.board[z]?0:quiet+1;s.board[z]=s.board[a];s.board[a]=0;s.turn=3-side;s.check=attacked(s.board,s.turn);checks.append(s.check?side:0);
            if(xlegal(s.board,s.turn).isEmpty()){s.winner=side;s.reason=s.check?"将死":"无子可动，困毙判负";}
            auto& seen=positions[key(s)];seen.append(checks.size());
            if(!s.winner&&seen.size()>=3){
                const int begin=seen[seen.size()-3];bool all[3]={false,true,true},any[3]={false,false,false};
                for(int i=begin;i<checks.size();++i){int player=i%2+1;any[player]=true;all[player]=all[player]&&checks[i]==player;}
                const bool one=all[1]&&any[1],two=all[2]&&any[2];s.winner=one!=two?(one?2:1):3;s.reason=one!=two?"长将判负":"三次重复局面，和棋";
            }else if(!s.winner&&quiet>=120){s.winner=3;s.reason="连续 120 手未吃子，和棋";}
        }
        s.moves.append(code);
    }
    if(!s.winner&&moves.size()==1024){s.winner=3;s.reason="房间达到步数上限，和棋";}
    result=s;return true;
}
}
