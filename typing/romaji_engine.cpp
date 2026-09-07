#include <stdint.h>
#include <stddef.h>

extern "C" {
void* memset(void* d, int v, size_t n) { unsigned char* p=(unsigned char*)d; for(size_t i=0;i<n;++i)p[i]=(unsigned char)v; return d; }
void* memcpy(void* d, const void* s, size_t n) { unsigned char* a=(unsigned char*)d; const unsigned char* b=(const unsigned char*)s; for(size_t i=0;i<n;++i)a[i]=b[i]; return d; }

static const int MAX_TOKENS=512;
static const int MAX_TOKEN_BYTES=64;
static const int MAX_TEXT_BYTES=4096;
static const int MAX_STREAM_BYTES=2048;
static const int MAX_CAND_BYTES=4096;
static const uint16_t MEMO_UNKNOWN=0;
static const uint16_t BOUNDARY_BASE=1024;

struct Token { char kana[MAX_TOKEN_BYTES]; int len; };

static char text_buf[MAX_TEXT_BYTES];
static char input_text_buf[MAX_TEXT_BYTES];
static Token tokens[MAX_TOKENS];
static int token_count=0;

/* The entire typed roman stream. Parsing is redone from this stream after each key.
   This is what makes ambiguous boundaries such as ん+ね robust. */
static char typed_stream[MAX_STREAM_BYTES];
static int typed_len=0;

static char input_buf[MAX_TOKEN_BYTES];
static int input_len=0;
static char fixed_buf[MAX_TOKENS][MAX_TOKEN_BYTES];
static int fixed_len[MAX_TOKENS];
static int idx_state=0;
static int ambiguous_end=0;

static char candidate_buf[MAX_CAND_BYTES];
static int candidate_len=0;

/* Memo: score + generation. MAX_TOKENS x MAX_STREAM_BYTES ~= 2M entries each. */
static uint16_t memo_score[MAX_TOKENS+1][MAX_STREAM_BYTES+1];
static uint16_t memo_tag[MAX_TOKENS+1][MAX_STREAM_BYTES+1];
static uint16_t generation=1;

static int slen(const char* s){int n=0;while(s[n])++n;return n;}
static void clear_bytes(char* p,int n){for(int i=0;i<n;++i)p[i]=0;}
static void smemcpy(char* d,const char* s,int n){for(int i=0;i<n;++i)d[i]=s[i];}
static bool same_n(const char* a,const char* b,int n){for(int i=0;i<n;++i)if(a[i]!=b[i])return false;return true;}
static bool ascii_equal(const char* a,int alen,const char* b){int blen=slen(b);return alen==blen&&same_n(a,b,blen);}
static bool starts_with(const char* s,int slen_,const char* p,int plen){return plen<=slen_&&same_n(s,p,plen);}

static int append_candidate(const char* s){int n=slen(s);if(!n||candidate_len+n+1>=MAX_CAND_BYTES)return 0;smemcpy(candidate_buf+candidate_len,s,n);candidate_len+=n;candidate_buf[candidate_len++]=0;return 1;}
static bool has_candidate(const char* s){int p=0;while(p<candidate_len){const char* c=candidate_buf+p;if(ascii_equal(c,slen(c),s))return true;p+=slen(c)+1;}return false;}
static void add_unique(const char* s){if(!has_candidate(s))append_candidate(s);}

static void add_basic(const char* kana){
  struct P{const char* k;const char* a[6];};
  static const P t[]={
    {"あ",{"a"}},{"い",{"i"}},{"う",{"u"}},{"え",{"e"}},{"お",{"o"}},
    {"か",{"ka"}},{"き",{"ki"}},{"く",{"ku"}},{"け",{"ke"}},{"こ",{"ko"}},
    {"さ",{"sa"}},{"し",{"shi","si","ci"}},{"す",{"su"}},{"せ",{"se"}},{"そ",{"so"}},
    {"た",{"ta"}},{"ち",{"chi","ti"}},{"つ",{"tsu","tu"}},{"て",{"te"}},{"と",{"to"}},
    {"な",{"na"}},{"に",{"ni"}},{"ぬ",{"nu"}},{"ね",{"ne"}},{"の",{"no"}},
    {"は",{"ha"}},{"ひ",{"hi"}},{"ふ",{"fu","hu"}},{"へ",{"he"}},{"ほ",{"ho"}},
    {"ま",{"ma"}},{"み",{"mi"}},{"む",{"mu"}},{"め",{"me"}},{"も",{"mo"}},
    {"や",{"ya"}},{"ゆ",{"yu"}},{"よ",{"yo"}},
    {"ら",{"ra"}},{"り",{"ri"}},{"る",{"ru"}},{"れ",{"re"}},{"ろ",{"ro"}},
    {"わ",{"wa"}},{"を",{"wo"}},
    {"が",{"ga"}},{"ぎ",{"gi"}},{"ぐ",{"gu"}},{"げ",{"ge"}},{"ご",{"go"}},
    {"ざ",{"za"}},{"じ",{"ji","zi"}},{"ず",{"zu"}},{"ぜ",{"ze"}},{"ぞ",{"zo"}},
    {"だ",{"da"}},{"ぢ",{"ji","di"}},{"づ",{"zu","du"}},{"で",{"de"}},{"ど",{"do"}},
    {"ば",{"ba"}},{"び",{"bi"}},{"ぶ",{"bu"}},{"べ",{"be"}},{"ぼ",{"bo"}},
    {"ぱ",{"pa"}},{"ぴ",{"pi"}},{"ぷ",{"pu"}},{"ぺ",{"pe"}},{"ぽ",{"po"}},
    {"ん",{"n","nn","n'"}},{"ー",{"-"}},{" ",{" "}},{"　",{" ","　"}},
    {nullptr,{nullptr}}
  };
  for(int i=0;t[i].k;++i){if(ascii_equal(kana,slen(kana),t[i].k)){for(int j=0;j<6&&t[i].a[j];++j)add_unique(t[i].a[j]);return;}}
  if(ascii_equal(kana,slen(kana),"ぁ")){add_unique("xa");add_unique("la");}
  else if(ascii_equal(kana,slen(kana),"ぃ")){add_unique("xi");add_unique("li");}
  else if(ascii_equal(kana,slen(kana),"ぅ")){add_unique("xu");add_unique("lu");}
  else if(ascii_equal(kana,slen(kana),"ぇ")){add_unique("xe");add_unique("le");}
  else if(ascii_equal(kana,slen(kana),"ぉ")){add_unique("xo");add_unique("lo");}
  else if(ascii_equal(kana,slen(kana),"ゃ")){add_unique("xya");add_unique("lya");}
  else if(ascii_equal(kana,slen(kana),"ゅ")){add_unique("xyu");add_unique("lyu");}
  else if(ascii_equal(kana,slen(kana),"ょ")){add_unique("xyo");add_unique("lyo");}
  else if(ascii_equal(kana,slen(kana),"ゎ")){add_unique("xwa");add_unique("lwa");}
}

static void add_youon(const char* r0,const char* r1,const char* r2,const char* r3,const char* vowel,const char* sx,const char* sl){
  const char* r[4]={r0,r1,r2,r3};
  for(int i=0;i<4;++i){if(!r[i])continue;char b[40];smemcpy(b,r[i],slen(r[i]));int q=slen(r[i]);smemcpy(b+q,vowel,slen(vowel));b[q+slen(vowel)]=0;add_unique(b);smemcpy(b,r[i],slen(r[i]));q=slen(r[i]);smemcpy(b+q,sx,slen(sx));b[q+slen(sx)]=0;add_unique(b);smemcpy(b,r[i],slen(r[i]));q=slen(r[i]);smemcpy(b+q,sl,slen(sl));b[q+slen(sl)]=0;add_unique(b);}
}

static void generate_normal_candidates(const char* kana){
  candidate_len=0; add_basic(kana);
  if(ascii_equal(kana,slen(kana),"きゃ"))add_youon("k","ky","ki",nullptr,"a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"きゅ"))add_youon("k","ky","ki",nullptr,"u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"きょ"))add_youon("k","ky","ki",nullptr,"o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"しゃ"))add_youon("sh","sy","si","shi","a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"しゅ"))add_youon("sh","sy","si","shi","u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"しょ"))add_youon("sh","sy","si","shi","o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"ちゃ"))add_youon("ch","ty","ti","chi","a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"ちゅ"))add_youon("ch","ty","ti","chi","u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"ちょ"))add_youon("ch","ty","ti","chi","o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"じゃ"))add_youon("j","jy","zy","zi","a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"じゅ"))add_youon("j","jy","zy","zi","u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"じょ"))add_youon("j","jy","zy","zi","o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"にゃ"))add_youon("n","ny","ni",nullptr,"a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"にゅ"))add_youon("n","ny","ni",nullptr,"u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"にょ"))add_youon("n","ny","ni",nullptr,"o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"ひゃ"))add_youon("h","hy","hi",nullptr,"a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"ひゅ"))add_youon("h","hy","hi",nullptr,"u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"ひょ"))add_youon("h","hy","hi",nullptr,"o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"みゃ"))add_youon("m","my","mi",nullptr,"a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"みゅ"))add_youon("m","my","mi",nullptr,"u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"みょ"))add_youon("m","my","mi",nullptr,"o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"りゃ"))add_youon("r","ry","ri",nullptr,"a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"りゅ"))add_youon("r","ry","ri",nullptr,"u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"りょ"))add_youon("r","ry","ri",nullptr,"o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"ぎゃ"))add_youon("g","gy","gi",nullptr,"a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"ぎゅ"))add_youon("g","gy","gi",nullptr,"u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"ぎょ"))add_youon("g","gy","gi",nullptr,"o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"ぢゃ"))add_youon("d","dy","di",nullptr,"a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"ぢゅ"))add_youon("d","dy","di",nullptr,"u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"ぢょ"))add_youon("d","dy","di",nullptr,"o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"びゃ"))add_youon("b","by","bi",nullptr,"a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"びゅ"))add_youon("b","by","bi",nullptr,"u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"びょ"))add_youon("b","by","bi",nullptr,"o","xyo","lyo");
  else if(ascii_equal(kana,slen(kana),"ぴゃ"))add_youon("p","py","pi",nullptr,"a","xya","lya");
  else if(ascii_equal(kana,slen(kana),"ぴゅ"))add_youon("p","py","pi",nullptr,"u","xyu","lyu");
  else if(ascii_equal(kana,slen(kana),"ぴょ"))add_youon("p","py","pi",nullptr,"o","xyo","lyo");

  if(ascii_equal(kana,slen(kana),"ふぁ")){add_unique("fa");add_unique("fwa");}
  else if(ascii_equal(kana,slen(kana),"ふぃ")){add_unique("fi");add_unique("fwi");}
  else if(ascii_equal(kana,slen(kana),"ふぇ")){add_unique("fe");add_unique("fwe");}
  else if(ascii_equal(kana,slen(kana),"ふぉ")){add_unique("fo");add_unique("fwo");}
  else if(ascii_equal(kana,slen(kana),"うぃ"))add_unique("wi");
  else if(ascii_equal(kana,slen(kana),"うぇ"))add_unique("we");
  else if(ascii_equal(kana,slen(kana),"うぉ")){add_unique("who");add_unique("wo");}
  else if(ascii_equal(kana,slen(kana),"しぇ")){add_unique("she");add_unique("sye");}
  else if(ascii_equal(kana,slen(kana),"じぇ")){add_unique("je");add_unique("jye");add_unique("zye");}
  else if(ascii_equal(kana,slen(kana),"ちぇ")){add_unique("che");add_unique("cye");add_unique("tye");}
  else if(ascii_equal(kana,slen(kana),"てぃ"))add_unique("thi");
  else if(ascii_equal(kana,slen(kana),"でぃ"))add_unique("dhi");
  else if(ascii_equal(kana,slen(kana),"とぅ"))add_unique("twu");
  else if(ascii_equal(kana,slen(kana),"どぅ"))add_unique("dwu");
  else if(ascii_equal(kana,slen(kana),"つぁ"))add_unique("tsa");
  else if(ascii_equal(kana,slen(kana),"つぃ"))add_unique("tsi");
  else if(ascii_equal(kana,slen(kana),"つぇ"))add_unique("tse");
  else if(ascii_equal(kana,slen(kana),"つぉ"))add_unique("tso");
  else if(ascii_equal(kana,slen(kana),"ゔぁ"))add_unique("va");
  else if(ascii_equal(kana,slen(kana),"ゔぃ"))add_unique("vi");
  else if(ascii_equal(kana,slen(kana),"ゔぇ"))add_unique("ve");
  else if(ascii_equal(kana,slen(kana),"ゔぉ"))add_unique("vo");
}

static int cp_len(const char* p,int rem){unsigned char c=(unsigned char)p[0];if(c<0x80)return 1;if((c&0xE0)==0xC0&&rem>=2)return 2;if((c&0xF0)==0xE0&&rem>=3)return 3;if((c&0xF8)==0xF0&&rem>=4)return 4;return 1;}
static bool is_small_utf8(const char* p){return same_n(p,"ゃ",3)||same_n(p,"ゅ",3)||same_n(p,"ょ",3)||same_n(p,"ぁ",3)||same_n(p,"ぃ",3)||same_n(p,"ぅ",3)||same_n(p,"ぇ",3)||same_n(p,"ぉ",3)||same_n(p,"ゎ",3);}

static void tokenize(){
  token_count=0; int p=0; int text_len=slen(text_buf);
  while(p<text_len && token_count<MAX_TOKENS){
    int n=cp_len(text_buf+p,text_len-p); Token* t=&tokens[token_count]; clear_bytes(t->kana,MAX_TOKEN_BYTES); smemcpy(t->kana,text_buf+p,n); t->len=n; p+=n;
    if(p<text_len){int m=cp_len(text_buf+p,text_len-p); if(m==3&&is_small_utf8(text_buf+p)&&t->len+m<MAX_TOKEN_BYTES){smemcpy(t->kana+t->len,text_buf+p,m);t->len+=m;p+=m;}}
    ++token_count;
  }
}

static bool is_sokuon(int i){return i>=0&&i<token_count&&ascii_equal(tokens[i].kana,tokens[i].len,"っ");}

static void generate_candidates(int i){
  candidate_len=0; if(i<0||i>=token_count)return;
  if(is_sokuon(i)){
    add_unique("xtu"); add_unique("ltu");
    if(i+1<token_count){
      generate_normal_candidates(tokens[i+1].kana);
      char tmp[MAX_CAND_BYTES]; int n=candidate_len; smemcpy(tmp,candidate_buf,n); candidate_len=0; add_unique("xtu");add_unique("ltu");
      int p=0; while(p<n){const char* s=tmp+p; if(s[0]&&s[0]>='a'&&s[0]<='z'&&s[0]!='n'&&s[0]!='a'&&s[0]!='e'&&s[0]!='i'&&s[0]!='o'&&s[0]!='u'&&s[0]!='y'&&s[0]!='w'){char one[2]={s[0],0};add_unique(one);}p+=slen(s)+1;}
    }
    return;
  }
  generate_normal_candidates(tokens[i].kana);
}

/* Returns best parse score from token index i / stream position pos.
   Boundary scores (>=1024) beat partial-prefix scores (<1024). */
static uint16_t best_score(int i,int pos){
  if(pos==typed_len) return (uint16_t)(BOUNDARY_BASE+i);
  if(i>=token_count) return 0;
  if(memo_tag[i][pos]==generation) return memo_score[i][pos];

  generate_candidates(i);
  const int list_len = candidate_len;
  char local_candidates[MAX_CAND_BYTES];
  smemcpy(local_candidates, candidate_buf, list_len);
  uint16_t best=0;
  int rem=typed_len-pos;
  int p=0;
  while(p<list_len){
    char cand[MAX_TOKEN_BYTES];
    const char* c=local_candidates+p; int clen=slen(c);
    if(clen >= MAX_TOKEN_BYTES) { p += clen + 1; continue; }
    smemcpy(cand,c,clen); cand[clen]=0;
    if(clen<=rem && same_n(typed_stream+pos,cand,clen)){
      uint16_t child=best_score(i+1,pos+clen);
      if(child>best)best=child;
    } else if(clen>rem && same_n(typed_stream+pos,cand,rem)){
      uint16_t partial=(uint16_t)(i+1);
      if(partial>best)best=partial;
    }
    p+=clen+1;
  }
  memo_tag[i][pos]=generation; memo_score[i][pos]=best; return best;
}

static void reset_memo(){++generation;if(generation==0){for(int i=0;i<=MAX_TOKENS;++i)for(int p=0;p<=MAX_STREAM_BYTES;++p)memo_tag[i][p]=0;generation=1;}}

static void recompute_parse(){
  idx_state=0; input_len=0; clear_bytes(input_buf,sizeof(input_buf)); ambiguous_end=0; for(int i=0;i<token_count;++i){fixed_len[i]=0;clear_bytes(fixed_buf[i],MAX_TOKEN_BYTES);}
  reset_memo(); uint16_t score=best_score(0,0); if(score==0||token_count==0)return;
  int i=0,pos=0;
  while(pos<typed_len && i<token_count){
    generate_candidates(i); const int list_len = candidate_len; char local_candidates[MAX_CAND_BYTES]; smemcpy(local_candidates,candidate_buf,list_len); int rem=typed_len-pos; bool chosen=false; int p=0;
    while(p<list_len){const char* c=local_candidates+p;int clen=slen(c);
      if(clen<=rem && same_n(typed_stream+pos,c,clen)){
        uint16_t child=best_score(i+1,pos+clen); if(child==score){smemcpy(fixed_buf[i],c,clen);fixed_buf[i][clen]=0;fixed_len[i]=clen;i++;pos+=clen;score=child;chosen=true;break;}
      }
      p+=clen+1;
    }
    if(chosen)continue;
    if(pos<typed_len){int left=typed_len-pos;if(left<MAX_TOKEN_BYTES){smemcpy(input_buf,typed_stream+pos,left);input_buf[left]=0;input_len=left;idx_state=i;return;}}
    break;
  }
  idx_state=i;
  if(pos==typed_len) { input_len=0; clear_bytes(input_buf,sizeof(input_buf)); }
  /* A final single n is intentionally considered ambiguous so JS can wait briefly.
     This permits both n and nn/n' at sentence end without making nn impossible. */
  if(idx_state==token_count && token_count>0 && fixed_len[token_count-1]==1 && fixed_buf[token_count-1][0]=='n'){
    generate_normal_candidates(tokens[token_count-1].kana); if(has_candidate("nn")||has_candidate("n'")) ambiguous_end=1;
  }
}

void engine_reset(int count){token_count=count<0?0:(count>MAX_TOKENS?MAX_TOKENS:count);typed_len=0;clear_bytes(typed_stream,MAX_STREAM_BYTES);idx_state=0;input_len=0;ambiguous_end=0;clear_bytes(input_buf,sizeof(input_buf));for(int i=0;i<MAX_TOKENS;++i){fixed_len[i]=0;clear_bytes(fixed_buf[i],MAX_TOKEN_BYTES);}reset_memo();}

void engine_set_text(const char* text,int len){if(!text||len<0)len=0;if(len>=MAX_TEXT_BYTES)len=MAX_TEXT_BYTES-1;if(text==text_buf){smemcpy(input_text_buf,text,len);text=input_text_buf;}clear_bytes(text_buf,MAX_TEXT_BYTES);smemcpy(text_buf,text,len);text_buf[len]=0;tokenize();engine_reset(token_count);}

int engine_input_text_ptr(){return (int)(uintptr_t)input_text_buf;}
int engine_text_ptr(){return (int)(uintptr_t)text_buf;}
int engine_text_len(){return slen(text_buf);}
int engine_count(){return token_count;}
int engine_idx(){return idx_state;}
int engine_buffer_ptr(){return (int)(uintptr_t)input_buf;}
int engine_buffer_len(){return input_len;}
int engine_fixed_ptr(int index){return index>=0&&index<MAX_TOKENS?(int)(uintptr_t)fixed_buf[index]:0;}
int engine_fixed_len(int index){return index>=0&&index<MAX_TOKENS?fixed_len[index]:0;}
int engine_candidate_ptr(){generate_candidates(idx_state);return (int)(uintptr_t)candidate_buf;}
int engine_candidate_len(){generate_candidates(idx_state);return candidate_len;}
int engine_candidate_ptr_at(int index){generate_candidates(index);return (int)(uintptr_t)candidate_buf;}
int engine_candidate_len_at(int index){generate_candidates(index);return candidate_len;}
int engine_finished(){return idx_state>=token_count && typed_len>0 && input_len==0;}
int engine_pending_end(){return ambiguous_end;}

int engine_key(int key){
  if(!((key>='a'&&key<='z')||key=='-'||key==' '))return 0;
  if(typed_len+1>=MAX_STREAM_BYTES)return 0;
  typed_stream[typed_len++]=(char)key; typed_stream[typed_len]=0;
  recompute_parse();
  if(best_score(0,0)==0){--typed_len;typed_stream[typed_len]=0;recompute_parse();return 0;}
  return input_len>0?1:2;
}

void engine_backspace(){if(typed_len>0){--typed_len;typed_stream[typed_len]=0;recompute_parse();}}

/* Force a pending final n to be treated as a completed single n. */
int engine_flush(){if(ambiguous_end){ambiguous_end=0;return 1;}return 0;}

void engine_sync(){}
}
