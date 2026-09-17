// Offline assessment of the real planner; no firmware behavior is changed.
// See scripts/audit_motion_accuracy.py for build, corpus run, and plots.
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "esp32_mock.hpp"
#include "../../lib/PolarControl/src/SCurve.cpp"
#include "../../lib/PolarControl/src/MotionPlanner.cpp"

// Reuse the existing native-only inspection friend. No production hooks.
struct MotionTimingTestAccess {
    static int head(const MotionPlanner& p) { return p.m_segmentHead; }
    static int tail(const MotionPlanner& p) { return p.m_segmentTail; }
    static const Segment& segment(const MotionPlanner& p, int i) { return p.m_segments[i]; }
    static int32_t thetaSteps(const MotionPlanner& p) { return p.m_executedTSteps.load(); }
    static int32_t rhoSteps(const MotionPlanner& p) { return p.m_executedRSteps.load(); }
    static int reproduceEndpointLoss() {
        resetMock();
        MotionPlanner p;
        p.init(400,1909,425,5.5,20,100,.225,2,10);
        p.addSegment(0,1);
        Segment& s=p.m_segments[0];
        SCurve::calculate(1,5.5,5.5,5.5,20,100,s.rho.profile);
        s.theta.profile={};s.theta.timeScale=s.rho.timeScale=1;
        s.duration=s.rho.profile.totalTime;s.calculated=true;
        s.lastGenTime=s.duration-STEP_TIMER_PERIOD_US/1000000.0f;
        s.lastGenRhoSteps=399;
        p.m_genSegmentIdx=0;p.m_genSegmentStartTime=0;
        // Horizon falls after the penultimate sample but before the endpoint.
        p.fillStepQueue(static_cast<uint32_t>((s.duration-.0001f)*1000000));
        if (!s.generationComplete) return 0;
        return s.rho.targetSteps-s.lastGenRhoSteps;
    }
};

constexpr double pi = 3.14159265358979323846;
constexpr double exactThetaScale = 12000.0 / (2.0 * pi);
constexpr int thetaScale = 1909;
constexpr int rhoScale = 400;
constexpr double radius = 425;
struct Point { double t, r; };
struct XY { double x, y; };
XY xy(Point p) { return {p.r * std::cos(p.t), p.r * std::sin(p.t)}; }
Point lerp(Point a, Point b, double u) { return {a.t+(b.t-a.t)*u, a.r+(b.r-a.r)*u}; }
double distance(XY a, XY b) { return std::hypot(a.x-b.x, a.y-b.y); }

// Closest distance to this source segment, not an unrelated crossing elsewhere
// in the drawing. Search each angular interval to handle multi-turn spirals.
double pathDistance(Point p, Point a, Point b) {
    XY q = xy(p);
    if (a.t == b.t) {
        const double r = std::clamp(q.x*std::cos(a.t)+q.y*std::sin(a.t),
                                  std::min(a.r,b.r), std::max(a.r,b.r));
        return distance(q, xy({a.t,r}));
    }
    if (a.r == b.r && std::abs(b.t-a.t) >= 2*pi) return std::abs(p.r-a.r);
    const int cells = std::max(1, int(std::ceil(std::abs(b.t-a.t)/(pi/4))));
    double best = std::min(distance(q,xy(a)),distance(q,xy(b)));
    for (int c=0;c<cells;++c) {
        double lo=double(c)/cells, hi=double(c+1)/cells;
        for (int k=0;k<32;++k) {
            double u=lo+(hi-lo)/3, v=hi-(hi-lo)/3;
            if (distance(q,xy(lerp(a,b,u))) < distance(q,xy(lerp(a,b,v)))) hi=v;
            else lo=u;
        }
        best=std::min(best,distance(q,xy(lerp(a,b,(lo+hi)/2))));
    }
    return best;
}

std::vector<Point> readPoints(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open "+path);
    std::vector<Point> points;
    std::string line;
    while (std::getline(in,line)) {
        std::replace(line.begin(),line.end(),',',' ');
        const auto start=line.find_first_not_of(" \t\r");
        if (start==std::string::npos || line[start]=='#' || line.compare(start,2,"//")==0) continue;
        std::istringstream s(line);
        Point p; std::string extra;
        if (!(s>>p.t>>p.r) || !std::isfinite(p.t) || !std::isfinite(p.r) ||
            p.r<0 || p.r>1 || ((s>>extra) && extra[0]!='#'))
            throw std::runtime_error("invalid THR in "+path);
        p.r*=radius;
        points.push_back(p);
    }
    if (points.size()<2) throw std::runtime_error("need two points");
    return points;
}

struct Stats {
    std::vector<double> shape, physical, shared;
    double duration=0, maxEndpoint=0, worst=0, maxTheta=0;
    size_t worstIndex=0, duplicates=0, stops=0;
    Segment worstSegment{};
    Point worstA{},worstB{};
};
double fraction(const AxisProfile& a, double time) {
    return a.profile.totalDistance>0 ? SCurve::getPosition(a.profile,
        float(time/a.timeScale))/a.profile.totalDistance : 0;
}
Point sample(const Segment& s, double time) {
    return {(s.theta.startSteps+fraction(s.theta,time)*s.theta.deltaSteps)/thetaScale,
            (s.rho.startSteps+fraction(s.rho,time)*s.rho.deltaSteps)/rhoScale};
}
void measure(const Segment& s, Point a, Point b, size_t index, Stats& out, int samples) {
    Point qa{double(s.theta.startSteps)/thetaScale,double(s.rho.startSteps)/rhoScale};
    Point qb{double(s.theta.targetSteps)/thetaScale,double(s.rho.targetSteps)/rhoScale};
    double shape=0,physical=0,shared=0;
    for (int i=0;i<=samples;++i) {
        Point p=sample(s,s.duration*double(i)/samples);
        shape=std::max(shape,pathDistance(p,qa,qb));
        // Ideal mechanism using exact nominal gear ratio, not planner's
        // integer scale. This excludes backlash, slip, and ball lag.
        const double ts=s.theta.startSteps+std::round(fraction(s.theta,s.duration*double(i)/samples)*s.theta.deltaSteps);
        const double rs=s.rho.startSteps+std::round(fraction(s.rho,s.duration*double(i)/samples)*s.rho.deltaSteps);
        physical=std::max(physical,pathDistance({ts/exactThetaScale,rs/rhoScale},a,b));
        // Geometric candidate only: common progress, exact scale, nearest
        // absolute step. It is not a streaming motion planner or timing test.
        Point reference=lerp(a,b,double(i)/samples);
        Point candidate{std::round(reference.t*exactThetaScale)/exactThetaScale,
                        std::round(reference.r*rhoScale)/rhoScale};
        shared=std::max(shared,pathDistance(candidate,a,b));
    }
    out.shape.push_back(shape); out.physical.push_back(physical); out.shared.push_back(shared);
    out.duration+=s.duration;
    out.maxEndpoint=std::max(out.maxEndpoint,distance(xy(b),xy({s.theta.targetSteps/exactThetaScale,s.rho.targetSteps/double(rhoScale)})));
    if (s.thetaExitVel<1e-6 && s.rhoExitVel<1e-6) ++out.stops;
    if (shape>out.worst) {
        out.worst=shape;out.worstIndex=index;out.worstSegment=s;out.worstA=a;out.worstB=b;
    }
}
void emitMetric(const char* name, std::vector<double> v) {
    std::sort(v.begin(),v.end());
    std::cout<<",\""<<name<<"\":{\"max\":"<<v.back()
             <<",\"p95_segment_max\":"<<v[size_t(.95*(v.size()-1))]
             <<",\"segments_over_0_25mm\":"<<std::count_if(v.begin(),v.end(),[](double x){return x>.25;})<<"}";
}

void selfTest() {
    auto require=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    require(std::abs(pathDistance({0,7},{0,2},{0,5})-2)<1e-8,"radial endpoint distance");
    require(std::abs(pathDistance({pi/4,8},{0,5},{pi/2,5})-3)<1e-5,"arc normal distance");
    require(pathDistance({5*pi,5},{0,5},{8*pi,5})<1e-8,"multi-turn circle distance");
    // Independent dense Cartesian oracle for short and multi-turn spirals.
    for (Point b:std::vector<Point>{{1,400},{-3,20},{9*pi,400}}) {
        Point a{0,100}, p=lerp(a,b,.371); p.r+=2;
        double dense=1e9;
        for(int i=0;i<=200000;++i) dense=std::min(dense,distance(xy(p),xy(lerp(a,b,double(i)/200000))));
        const double found=pathDistance(p,a,b);
        require(found<=dense+1e-5 && dense-found<.005,"nearest-path oracle mismatch");
    }
    // A feasible shared-progress S-curve for a representative mixed move.
    // Constraints are projected to scalar progress; both axes use the SAME u.
    const double dt=1.2,dr=300;
    SCurve::Profile scalar{};
    require(SCurve::calculate(1,0,0,std::min(.225/dt,5.5/dr),
            std::min(2/dt,20/dr),std::min(10/dt,100/dr),scalar),"shared profile solve");
    double maxError=0;
    for(int i=0;i<=1000;++i) {
        float t=scalar.totalTime*i/1000;
        double u=SCurve::getPosition(scalar,t);
        double v=SCurve::getVelocity(scalar,t),a=SCurve::getAcceleration(scalar,t);
        require(v*dt<=.225001 && v*dr<=5.50001 && std::abs(a)*dt<=2.00001 && std::abs(a)*dr<=20.00001,"shared axis limits");
        maxError=std::max(maxError,pathDistance({dt*u,50+dr*u},{0,50},{dt,350}));
    }
    require(maxError<.001,"shared profile geometry");
    // Numerical reproductions of production hazards, not claimed fixes.
    volatile float longTime=1024.0f, firmwareInterval=50.0f/1000000.0f;
    require(longTime+firmwareInterval==longTime,"float horizon reproduction");
    const double closure=distance(xy({2*pi,425}),xy({int(float(2*pi)*thetaScale)/exactThetaScale,425}));
    const int endpointLoss=MotionTimingTestAccess::reproduceEndpointLoss();
    require(endpointLoss==1,"horizon endpoint-loss reproduction");
    std::cout<<"{\"self_test\":\"pass\",\"shared_profile_error_mm\":"<<maxError
             <<",\"shared_rest_to_rest_seconds\":"<<scalar.totalTime
             <<",\"integer_scale_one_turn_closure_mm\":"<<closure
             <<",\"float_50us_increment_stalls_at_seconds\":1024"
             <<",\"horizon_endpoint_lost_steps\":"<<endpointLoss<<"}\n";
}

int main(int argc,char** argv) try {
    if (argc==2 && std::string(argv[1])=="--self-test") { selfTest();return 0; }
    if (argc<2) throw std::runtime_error("usage: accuracy_audit pattern.thr [samples=32] [speed=1]");
    const int samples=argc>2?std::stoi(argv[2]):32;
    const float speed=argc>3?std::stof(argv[3]):1;
    if (samples<2 || !std::isfinite(speed) || speed<.1 || speed>1) throw std::runtime_error("invalid samples or speed");
    auto points=readPoints(argv[1]);
    resetMock();
    MotionPlanner planner;
    planner.init(rhoScale,thetaScale,radius,5.5,20,100,.225,2,10);
    planner.setSpeedMultiplier(speed);
    // Exclude travel to the first point: it is not a THR segment.
    planner.resetPosition(float(points[0].t),float(float(points[0].r/radius)*radius));
    std::array<size_t,SEGMENT_BUFFER_SIZE> source{};
    size_t next=1, measured=0;
    bool started=false;
    Stats stats;
    for (const auto& p:points) stats.maxTheta=std::max(stats.maxTheta,std::abs(p.t));
    for (uint64_t iteration=0;iteration<100000000;++iteration) {
        bool added=false;
        while (next<points.size() && planner.hasSpace()) {
            const int slot=MotionTimingTestAccess::head(planner);
            const auto& p=points[next];
            if (!planner.addSegment(float(p.t),float(float(p.r/radius)*radius))) throw std::runtime_error("point rejected");
            if (MotionTimingTestAccess::head(planner)!=slot) source[slot]=next;
            else ++stats.duplicates;
            ++next;added=true;
        }
        planner.setEndOfPattern(next==points.size());
        if (added) planner.recalculate();
        if (!started) { planner.start();started=true; }
        planner.process();
        const int slot=MotionTimingTestAccess::tail(planner);
        if (slot!=MotionTimingTestAccess::head(planner)) {
            const auto& s=MotionTimingTestAccess::segment(planner,slot);
            if (s.executing && source[slot]>measured) {
                const size_t i=source[slot];
                measure(s,points[i-1],points[i],i,stats,samples);
                measured=i;
            }
        }
        advanceMicros(10000);
        if (next==points.size() && planner.isIdle()) break;
        if (iteration==99999999) throw std::runtime_error("simulation timed out");
    }
    if (stats.shape.empty() || stats.shape.size()+stats.duplicates!=points.size()-1)
        throw std::runtime_error("segment coverage mismatch");
    PlannerTelemetry telemetry; planner.getTelemetry(telemetry);
    const auto& last=points.back();
    const int expectedT=int(float(last.t)*thetaScale);
    const int expectedR=int(float(float(last.r/radius)*radius)*rhoScale);
    const int finalTError=MotionTimingTestAccess::thetaSteps(planner)-expectedT;
    const int finalRError=MotionTimingTestAccess::rhoSteps(planner)-expectedR;
    std::cout<<std::setprecision(10)<<"{\"points\":"<<points.size()<<",\"segments\":"<<stats.shape.size()
             <<",\"samples_per_segment\":"<<samples+1<<",\"speed\":"<<speed
             <<",\"duplicates\":"<<stats.duplicates<<",\"planned_seconds\":"<<stats.duration
             <<",\"both_axes_stop\":"<<stats.stops<<",\"underruns\":"<<telemetry.underruns
             <<",\"final_theta_step_error\":"<<finalTError<<",\"final_rho_step_error\":"<<finalRError
             <<",\"max_abs_theta\":"<<stats.maxTheta<<",\"max_endpoint_mm\":"<<stats.maxEndpoint;
    emitMetric("profile_shape_mm",stats.shape); emitMetric("nominal_mechanism_mm",stats.physical);
    emitMetric("common_progress_exact_scale_mm",stats.shared);
    std::cout<<",\"worst_shape_point_index\":"<<stats.worstIndex<<",\"worst_trace\":[";
    const auto& s=stats.worstSegment;
    for (int i=0;i<=128;++i) {
        auto actual=xy(sample(s,s.duration*double(i)/128));
        auto desired=xy(lerp({s.theta.startSteps/double(thetaScale),s.rho.startSteps/double(rhoScale)},
                             {s.theta.targetSteps/double(thetaScale),s.rho.targetSteps/double(rhoScale)},double(i)/128));
        if(i)std::cout<<",";
        std::cout<<"["<<desired.x<<","<<desired.y<<","<<actual.x<<","<<actual.y<<"]";
    }
    std::cout<<"]}\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n";return 1; }
