static void verify(FArchiveFileLoad& f,int pos,int count) {
    f.Seek(pos);
    std::vector<BYTE> got(count);
    f.Serialize(got.data(),count);
    assert(std::equal(got.begin(),got.end(),fixture.begin()+pos));
    assert(f.Tell()==pos+count);
    assert(f.Pos==pos+count);
}
template<class Fn> static void expectError(Fn fn) {
    bool caught=false;
    try { fn(); } catch(const std::runtime_error&) { caught=true; }
    assert(caught);
}
int main() {
    fixture.resize(300123);
    std::mt19937 rng(9876);
    for(auto& b:fixture) b=rng();
    for(int enabled=0;enabled<=1;++enabled) {
        options=enabled?"-MGLREADCACHE=1":"-MGLREADCACHE=0";
        FArchiveFileLoad f("mock");
        reads=seeks=0;
        for(int i=0;i<65536;++i) {
            BYTE b; f.Serialize(&b,1); assert(b==fixture[i]);
        }
#ifdef UE_MINIGL_READCACHE
        assert(reads==(enabled?1:65536));
#else
        assert(reads==65536);
#endif
        assert(seeks==0);
        verify(f,65530,20); // Cross the cache boundary.
        verify(f,0,140000); // Large direct read, possibly after a cache hit.
        verify(f,300100,23); // Tail shorter than a buffer.
        f.Serialize(nullptr,0); assert(f.Tell()==300123);
        verify(f,0,0);
        for(int i=0;i<2000;++i) {
            int pos=rng()%(fixture.size()+1);
            int count=rng()%150000;
            count=std::min(count,(int)fixture.size()-pos);
            verify(f,pos,count);
            FFileStatus outer,inner;
            f.Push(outer,nullptr);
            verify(f,rng()%200000,33);
            f.Push(inner,nullptr);
            verify(f,rng()%200000,77);
            f.Pop(inner); verify(f,inner.SavedPos,9);
            f.Pop(outer); assert(f.Tell()==pos+count);
            verify(f,pos+count,std::min(11,(int)fixture.size()-pos-count));
        }
#ifdef UE_MINIGL_READCACHE
        if(enabled) {
            // Cached backwards seeks need neither fread nor fseek.
            verify(f,100,1); int oldReads=reads,oldSeeks=seeks;
            verify(f,110,1); verify(f,100,1);
            assert(reads==oldReads && seeks==oldSeeks);
            expectError([&]{f.Serialize(nullptr,-1);});
            expectError([&]{f.Serialize(nullptr,INT_MAX);});
            f.Seek(300123); expectError([&]{BYTE b;f.Serialize(&b,1);});
            shortRead=1;
            f.Seek(280000); expectError([&]{BYTE b;f.Serialize(&b,1);});
            shortRead=0;
            verify(f,280000,20); // Failed refill was not published as valid data.
        }
#endif
    }
#ifdef UE_MINIGL_READCACHE
    options="";
    { FArchiveFileLoad f("mock"); assert(f.UseReadCache==1);
      failSeek=1; f.Seek(250000);
      expectError([&]{BYTE b;f.Serialize(&b,1);}); failSeek=0; }
    { FArchiveFileLoad f("mock"); shortRead=1;
      expectError([&]{std::vector<BYTE> b(100000);f.Serialize(b.data(),b.size());});
      shortRead=0; verify(f,0,100000); }
    { FArchiveFileLoad first("mock"),second("mock");
      verify(first,123,17); verify(second,200000,90);
      verify(first,140,27); verify(second,200090,42); }
#endif
    fixture.clear();
    { FArchiveFileLoad empty("mock"); empty.Seek(0); empty.Serialize(nullptr,0); assert(empty.Tell()==0); }
    { FArchiveFileLoad unused; }
    std::puts("Read cache: data, random seeks, nested Push/Pop, EOF and error checks passed.");
}
