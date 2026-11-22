#pragma once
#include <QJsonObject>
#include <QVector>
#include <algorithm>
#include <cmath>
namespace Messager {
// Rendering/input history only. Never advances an authoritative game or awards points.
class RealtimeSync {
public:
    struct Input {int tick;QJsonObject value;};
    QVector<Input> pending;
    int tick=0,sentTick=0,ack=0,corrections=0;
    double maxCorrection=0;
    // 重置游戏联机。
    void reset(){*this=RealtimeSync();}
    // 记录游戏联机。
    int record(QJsonObject value){pending.append({++tick,value});while(pending.size()>32)pending.removeFirst();return tick;}
    // 更新游戏联机中的sent状态。
    void sent(){sentTick=tick;}
    // 判断给定的接收确认数据是否有效。
    bool validAck(const QJsonValue& value)const{return value.isDouble()&&std::isfinite(value.toDouble())&&value.toDouble()==value.toInt(-1)&&value.toInt()>=ack&&value.toInt()<=sentTick;}
    // 更新游戏联机中的快照状态。
    void snapshot(qint64 now,int acknowledged,qint64 echo){
        ack=acknowledged;while(!pending.isEmpty()&&pending.first().tick<=ack)pending.removeFirst();
        if(lastSnapshot>=0){const auto interval=now-lastSnapshot;if(interval>0&&interval<1000){arrivals.append(int(interval));if(arrivals.size()>64)arrivals.removeFirst();}}
        lastSnapshot=now;
        if(echo>=0&&echo<=now&&now-echo<=5000){rtts.append(int(now-echo));if(rtts.size()>120)rtts.removeFirst();}
    }
    // 清除预测。
    void clearPrediction(){pending.clear();}
    // 返回游戏联机使用的插值毫秒。
    int interpolationMs()const{return arrivals.isEmpty()?40:std::clamp(percentile(arrivals,0.90),25,80);}
    // 返回游戏联机使用的rtt毫秒。
    int rttMs()const{return rtts.isEmpty()?-1:rtts.last();}
    // 返回游戏联机使用的p95毫秒。
    int p95Ms()const{return rtts.isEmpty()?-1:percentile(rtts,0.95);}
    // 更新游戏联机中的修正后的状态。
    void corrected(double distance){++corrections;maxCorrection=qMax(maxCorrection,distance);}
    // 返回游戏联机使用的统计信息。
    QJsonObject stats()const{return {{"rtt_ms",rttMs()},{"p95_ms",p95Ms()},{"interpolation_ms",interpolationMs()},{"pending_inputs",pending.size()},{"corrections",corrections},{"max_correction",maxCorrection}};}
private:
    // 从延迟样本计算指定分位数，供实时联机统计显示。
    static int percentile(QVector<int> list,double q){std::sort(list.begin(),list.end());return list[qMin(list.size()-1,qsizetype(std::ceil(list.size()*q)-1))];}
    QVector<int> arrivals,rtts;qint64 lastSnapshot=-1;
};
}
