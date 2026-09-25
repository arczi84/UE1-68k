#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

enum { GL_TRIANGLES=4, GL_TRIANGLE_FAN=6 };
struct Vertex {
    int poly,index; float u,v,x,y,z;
    bool operator==(const Vertex& b) const {
        return poly==b.poly && index==b.index && u==b.u && v==b.v && x==b.x && y==b.y && z==b.z;
    }
};
struct Call { int mode; std::vector<Vertex> vertices; };
static std::vector<Call> calls;
static bool open=false;
static void glBegin(int mode) {
    assert(!open); open=true; calls.push_back({mode,{}});
}
static void glEnd() {
    assert(open); open=false;
    if(calls.back().mode==GL_TRIANGLES) {
        assert(!calls.back().vertices.empty());
        assert(calls.back().vertices.size()<=384);
        assert(calls.back().vertices.size()%3==0);
    }
}
#include "../../Source/NOpenGLDrv/SurfaceBatch.h"
struct Poly { int NumPts,id; Poly* Next; };
static Vertex vertex(Poly* p,int i) {
    return {p->id,i,i*.25f,p->id*.125f,i*1.25f,p->id*2.f,i*.5f+3.f};
}
static void emit(Poly* p,int i) {
    assert(open && i>=0 && i<p->NumPts);
    calls.back().vertices.push_back(vertex(p,i));
}
static void fan(Poly* p) {
    glBegin(GL_TRIANGLE_FAN);
    for(int i=0;i<p->NumPts;++i) emit(p,i);
    glEnd();
}
static void test(const std::vector<int>& sizes) {
    std::vector<Poly> polys(sizes.size());
    std::vector<Vertex> expected;
    unsigned long inputs=0;
    for(unsigned i=0;i<sizes.size();++i) {
        polys[i]={sizes[i],(int)i,i+1<sizes.size()?&polys[i+1]:nullptr};
        inputs+=sizes[i];
        for(int k=1;k+1<sizes[i];++k) {
            expected.push_back(vertex(&polys[i],0));
            expected.push_back(vertex(&polys[i],k));
            expected.push_back(vertex(&polys[i],k+1));
        }
    }
    for(int mode=0;mode<=1;++mode) for(int counters=0;counters<=1;++counters) {
        calls.clear(); FSurfaceBatchStats stats={};
        DrawSurfacePolys(polys.empty()?nullptr:&polys[0],mode,counters?&stats:nullptr,emit,fan);
        assert(!open); // No deferred vertices across pass/state boundaries.
        std::vector<Vertex> actual;
        for(const auto& call:calls) {
            if(call.mode==GL_TRIANGLES) actual.insert(actual.end(),call.vertices.begin(),call.vertices.end());
            else for(unsigned i=1;i+1<call.vertices.size();++i) {
                actual.push_back(call.vertices[0]);
                actual.push_back(call.vertices[i]);
                actual.push_back(call.vertices[i+1]);
            }
        }
        assert(actual==expected); // Order, fan winding and all attributes.
        assert(calls.size()==(mode?(expected.size()+383)/384:sizes.size()));
        if(counters) {
            assert(stats.Passes==1 && stats.Fans==sizes.size());
            assert(stats.Calls==calls.size() && stats.InputVertices==inputs);
            assert(stats.SubmittedVertices==(mode?expected.size():inputs));
        } else assert(stats.Passes==0);
    }
}
int main() {
    test({}); test({0,1,2}); test({3}); test({4,5,3,0,2,7});
    test({130}); test({131}); test({10000});
    test(std::vector<int>(128,3)); test(std::vector<int>(129,3));
    test(std::vector<int>(1000,4));
    std::mt19937 rng(21);
    for(int t=0;t<150;++t) {
        std::vector<int> sizes(rng()%50);
        for(auto& size:sizes) size=rng()%180;
        test(sizes);
    }
    // Separate passes never share one glBegin/glEnd even if texture is equal.
    Poly p={3,0,nullptr}; FSurfaceBatchStats totals={}; calls.clear();
    DrawSurfacePolys(&p,true,&totals,emit,fan); assert(!open);
    DrawSurfacePolys(&p,true,&totals,emit,fan); assert(!open);
    assert(totals.Passes==2 && totals.Calls==2 && totals.Fans==2);
    std::puts("Surface batching: triangle order/attributes, bounds, pass isolation, fallback and counters passed.");
}
