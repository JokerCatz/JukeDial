// SPDX-License-Identifier: LGPL-2.1-or-later
/* The settings page's own script (index.html loads it after the libraries; flashtab.js and fonttab.js follow and use its $, el, toast, dev). No inline script: the page's CSP allows scripts from this origin only. */
/* every CSP violation of this page is kept for the tests (and the console): window.__csp */
window.__csp=[];document.addEventListener('securitypolicyviolation',e=>{window.__csp.push(e.violatedDirective+' '+e.blockedURI);console.warn('CSP',e.violatedDirective,e.blockedURI)});
const $=s=>document.querySelector(s);
function el(t,a={},...kids){const e=document.createElement(t);for(const[k,v]of Object.entries(a)){if(k=='on')for(const[ev,f]of Object.entries(v))e.addEventListener(ev,f);else if(k in e)e[k]=v;else e.setAttribute(k,v)}for(const c of kids)if(c!=null)e.append(c);return e}
let toastT=0;function toast(t,bad){const x=$('#toast');x.textContent=t;x.className='show'+(bad?' bad':'');clearTimeout(toastT);toastT=setTimeout(()=>x.className='',2600)}
const fmt=(v,step)=>{const d=step>=1?0:step>=.1?1:2;return(+v).toFixed(d)};

/* ================= connection: Conn (conn.js) is the only owner of the SerialPort; this page only asks it ================= */
Conn.setMessages(c=>({notconn:T('W_NOT_CONNECTED_YET'),nosupport:T('W_THIS_BROWSER_HAS_NO_WEB_SERIAL_USE_DESKTOP_C'),timeout:T('W_THE_PLAYER_DID_NOT_ANSWER'),lost:T('W_CONNECTION_LOST'),busy:T('W_FLASHING_PLEASE_DO_NOT_TOUCH_ANYTHING'),device:T('W_FAILED')}[c]));
async function connect(){
 try{if(!await Conn.open())return}
 catch(e){if(e.code=='nosupport')toast(e.message,1);else toast(T('W_COULD_NOT_CONNECT_0_1_IF_IT_SAYS_FAILED_TO_O',e.name,e.message),1);console.error(e);return}
 setConnected(true);await refreshAll()}   /* connected = after the quiet window; a request issued earlier waits for it (Conn.request) */
function setConnected(on){$('#conn').textContent=on?T('W_DISCONNECT'):T('W_CONNECT');$('#conn').classList.toggle('acc',!on);$('#conn-pill').textContent=on?T('W_CONNECTED'):T('W_NOT_CONNECTED');$('#conn-pill').classList.toggle('on',on);$('#guide').hidden=on;$('#nowbar').hidden=!on;$('#volbar').hidden=!on}
Conn.on('log',line=>{const log=$('#log');log.textContent+=line+'\n';if(log.textContent.length>60000)log.textContent=log.textContent.slice(-40000)});
Conn.on('close',byUser=>{setConnected(false);if(byUser)toast(T('W_DISCONNECTED_THE_SERIAL_PORT_IS_RELEASED'));else toast(T('W_CONNECTION_LOST_THE_PLAYER_REBOOTED_OR_THE_C'),1)});
const dev=(op,args,ms)=>Conn.request(op,args,ms);
$('#conn').onclick=()=>{if(!Conn.up)return connect();Conn.close()};
async function refreshAll(){await status();await loadStations().catch(e=>toast(e.message,1));fxLoaded=false;cfgLoaded=false;const t=document.querySelector('nav button.sel').dataset.t;if(t=='fx')loadFx();if(t=='lists')loadDeviceLists();if(t=='dev')loadCfg()}

/* tabs */
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('sel',x==b));document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.id==b.dataset.t));
 if(b.dataset.t=='fx'&&Conn.up&&!fxLoaded)loadFx();if(b.dataset.t=='lists')loadDeviceLists();if(b.dataset.t=='dev'&&Conn.up&&!cfgLoaded)loadCfg()});
initLists();
{const t=document.querySelector(`nav button[data-t="${location.hash.slice(1).split('/')[0]}"]`);if(t)setTimeout(()=>{t.click();scrollTo(0,0)})}

/* ================= status bar ================= */
let playingIdx=-1,lastStatus=null;
async function status(){if(!Conn.up)return;try{const s=await dev('status');lastStatus=s;Lang.fromDevice(s.lang);if(window.flashTab)flashTab.setBoard(s.board);
 const[t1,t2]=(s.playing||'').split('\n');$('#nowt').textContent=s.playing?t1:T('W_NOTHING_IS_PLAYING');$('#nows').textContent=s.playing?(t2||''):T('W_PRESS_IN_MY_STATIONS_TO_PLAY');$('#stopbtn').disabled=!s.playing;
 if(document.activeElement!=$('#vol')){$('#vol').value=s.volume;$('#volv').textContent=s.volume}
 $('#fwifi').textContent=s.wifi?T('W_THE_PLAYER_S_WI_FI_IS_CONNECTED_CHECK_ON_PLA'):T('W_THE_PLAYER_S_WI_FI_IS_NOT_CONNECTED_CHECK_ON');
 $('#wifistat').textContent=s.wifi?T('W_CONNECTED_SIGNAL_0_DBM',s.rssi):T('W_NOT_CONNECTED_TO_WI_FI_IT_ONLY_CONNECTS_WHEN');
 $('#mem').textContent=T('W_MEMORY_INTERNAL_0_KB_FREE_LOWEST_1_KB_PSRAM',(s.internal_free/1024).toFixed(0),(s.internal_min/1024).toFixed(0),(s.psram_free/1048576).toFixed(1));
 if(s.radio!=playingIdx){playingIdx=s.radio;drawStations()}}catch(e){}}
setInterval(status,2000);
$('#stopbtn').onclick=async()=>{await dev('stop').catch(()=>{});status()};
let volT=0;$('#vol').oninput=()=>{$('#volv').textContent=$('#vol').value;clearTimeout(volT);volT=setTimeout(()=>dev('volume',{i:+$('#vol').value}).catch(()=>{}),80)};

/* ================= preview in this browser, and what the device can play ================= */
const OK_CODECS=/^(mp3|aac|aac\+|he-aac|mpeg)/i;
let preUrl='',preBtn=null;
function stopPreview(){const a=$('#pre');a.pause();a.removeAttribute('src');if(preBtn)preBtn.textContent=T('W_PREVIEW_2');preUrl='';preBtn=null}
/* plays url here; resolves true once sound starts, false on error or after 8 s */
function preview(url,btn){if(/^http:/i.test((url||'').trim())){toast(T('W_PREVIEW_HTTP_ONLY'),1);return Promise.resolve(false)}   /* the page's CSP allows https media only (an http stream is mixed content on an HTTPS page anyway) */
 const a=$('#pre');if(preUrl==url){stopPreview();return Promise.resolve(null)}stopPreview();preUrl=url;preBtn=btn;if(btn)btn.textContent=T('W_STOP');
 return new Promise(res=>{let done=false;const fin=v=>{if(done)return;done=true;a.onplaying=a.onerror=null;if(!v&&preUrl==url)stopPreview();res(v)};
  a.onplaying=()=>fin(true);a.onerror=()=>fin(false);setTimeout(()=>fin(false),8000);a.src=url;a.play().catch(()=>fin(false))})}
const isHttp=u=>/^http:\/\//i.test((u||'').trim());

/* ================= my stations ================= */
let st=[],editing=false,saveT=0;
let curList='radio',rlists=[];
function gotStations(r){st=r.stations.map(s=>({...s,alts:s.alts||[]}));curList=r.list||'radio';rlists=r.lists||[];drawListSel();drawStations()}
function drawListSel(){const S=$('#rlsel');$('#rlrow').hidden=!Conn.up;S.replaceChildren(...rlists.map(n=>el('option',{value:n,selected:n==curList},n=='radio'?T('W_RADIO_DEFAULT'):n)))}
async function loadStations(){gotStations(await dev('stations_get'))}
$('#rlsel').onchange=async()=>{try{if(saveT)await saveStations();gotStations(await dev('radio_list',{list:$('#rlsel').value}));toast(T('W_LIST_SWITCHED'))}catch(e){toast(e.message,1);drawListSel()}};
$('#rlnew').onclick=async()=>{const n=(prompt(T('W_NAME_OF_THE_NEW_LIST_LOWERCASE_LETTERS_DIGIT'))||'').trim();if(!n)return;if(!/^[a-z0-9_-]{1,23}$/.test(n)||n=='radio')return toast(T('W_THE_NAME_MAY_ONLY_USE_LOWERCASE_LETTERS_DIGI'),1);
 try{gotStations(await dev('radio_list',{list:n,create:true}));toast(T('W_LIST_CREATED_EMPTY_ADD_STATIONS_FROM_ADD_A_U'))}catch(e){toast(e.message,1)}};
function saveSoon(){clearTimeout(saveT);saveT=setTimeout(saveStations,700)}
async function saveStations(){clearTimeout(saveT);saveT=0;try{const r=await dev('stations_put',{list:curList,stations:st.map(s=>({name:s.name.trim(),url:s.url.trim(),alts:(s.alts||[]).map(u=>u.trim()).filter(Boolean).slice(0,2)}))});toast(T('W_SAVED_0_STATIONS',r.stations))}catch(e){toast(T('W_SAVE_FAILED')+e.message,1)}}
function drawStations(){const L=$('#rlist');$('#rlrow').hidden=!Conn.up;L.replaceChildren();L.classList.toggle('edit',editing);L.classList.remove('dim');
 if(!Conn.up){L.textContent=T('W_THE_PLAYER_S_STATION_LIST_SHOWS_HERE_AFTER_Y');L.classList.add('dim');return}
 if(!st.length){L.textContent=T('W_NO_STATIONS_YET');return}
 st.forEach((s,i)=>{const playing=i==playingIdx;const row=el('div',{className:'st'+(playing?' playing':'')});
  if(!editing){row.append(el('button',{className:'icon'+(playing?' acc':''),title:playing?T('W_STOP_2'):T('W_PLAY_ON_THE_PLAYER'),on:{click:()=>playOnDevice(i)}},playing?'■':'▶'),
    el('div',{className:'t'},el('div',{className:'name ell'},s.name),el('div',{className:'dim small ell'},(s.scheme?'['+s.scheme+'] ':'')+s.url+((s.alts||[]).length?T('W_0_BACKUP_S',s.alts.length):''))),
    ...(isHttp(s.url)?[]:[el('button',{className:'sm',title:T('W_TRY_THE_HTTP_VERSION_IF_IT_PLAYS_SWAP_THE_UR'),on:{click:e=>fixStation(i,e.target)}},T('W_TRY_TO_FIX'))]))}
  else{row.append(el('div',{className:'t',style:'flex:1 1 100%'},
     el('input',{value:s.name,on:{input:e=>{s.name=e.target.value;saveSoon()}}}),
     el('input',{value:s.url,style:'margin-top:6px',on:{input:e=>{s.url=e.target.value;saveSoon()}}}),
     el('input',{value:(s.alts||[]).join(' '),placeholder:T('W_BACKUP_URLS_OPTIONAL_AT_MOST_2_SEPARATED_BY'),style:'margin-top:6px',on:{input:e=>{s.alts=e.target.value.split(/\s+/).filter(Boolean).slice(0,2);saveSoon()}}})),
    el('div',{className:'row'},el('button',{className:'sm',disabled:i==0,on:{click:()=>{[st[i-1],st[i]]=[st[i],st[i-1]];drawStations();saveSoon()}}},T('W_MOVE_UP')),
     el('button',{className:'sm',disabled:i==st.length-1,on:{click:()=>{[st[i+1],st[i]]=[st[i],st[i+1]];drawStations();saveSoon()}}},T('W_MOVE_DOWN')),
     el('button',{className:'sm',on:{click:()=>{if(st.length<=1)return toast(T('W_AT_LEAST_ONE_STATION_MUST_STAY'),1);if(confirm(T('W_DELETE_0',s.name))){st.splice(i,1);drawStations();saveSoon()}}}},T('W_DELETE'))))}
  L.append(row)})}
/* an https station: try its http twin in this browser; if it plays, keep that address */
/* the device itself decides: it connects (http only, following http redirects) and looks at the bytes */
const WHY=()=>({https:T('W_REDIRECTS_TO_HTTPS_THE_PLAYER_DOES_NOT_SUPPO'),format:T('W_FORMAT_NOT_SUPPORTED_BY_THE_PLAYER'),wifi:T('W_THE_PLAYER_IS_NOT_ON_WI_FI'),redirects:T('W_TOO_MANY_REDIRECTS'),'no answer':T('W_CONNECTION_TIMED_OUT_NO_ANSWER'),refused:T('W_THE_OTHER_SIDE_REFUSED_THE_CONNECTION'),'empty playlist':T('W_THE_PLAYLIST_IS_EMPTY'),'hls list':T('W_THE_HLS_PLAYLIST_COULD_NOT_BE_READ'),memory:T('W_THE_PLAYER_IS_OUT_OF_MEMORY')});
const why=w=>WHY()[w]||(/^status /.test(w)?T('W_SERVER_REPLIED')+w.slice(7):w||T('W_CANNOT_PLAY'));
const FMT={mp3:'MP3',aac:'AAC',ogg:'OGG',ts:'TS',hls:'HLS'};
async function probe(url){if(!Conn.up)throw new Error(T('W_CONNECT_TO_THE_PLAYER_FIRST'));
 if(!lastStatus||!lastStatus.wifi)toast(T('W_THE_PLAYER_IS_CONNECTING_TO_WI_FI_ABOUT_10_S'));
 const r=await dev('probe',{url},30000).catch(e=>{throw /^busy/.test(e.message)?new Error(T('W_THE_PLAYER_IS_CHECKING_ANOTHER_ONE_PRESS_AGA')):e});if(r.why=='wifi')toast(T('W_THE_PLAYER_CANNOT_REACH_WI_FI_ENTER_IT_ON_TH'),1);return r}
/* an https station: ask the device about its http twin; keep it if the device can play it */
async function fixStation(i,btn){const s=st[i],u=s.url.trim().replace(/^https:/i,'http:');btn.textContent=T('W_CHECKING_ON_THE_PLAYER');btn.disabled=true;
 try{const r=await probe(u);if(r.playable){s.url=u;await saveStations();drawStations();toast(T('W_CHANGED_TO_THE_HTTP_VERSION_0_THE_PLAYER_CAN',FMT[r.fmt]||r.fmt))}
  else{btn.textContent=T('W_CANNOT_FIX');toast(T('W_THE_PLAYER_CANNOT_PLAY_IT')+why(r.why),1)}}catch(e){btn.textContent=T('W_TRY_TO_FIX');btn.disabled=false;toast(e.message,1)}}
$('#editbtn').onclick=()=>{editing=!editing;$('#editbtn').textContent=editing?T('W_DONE'):T('W_EDIT');if(!editing)saveStations();drawStations()};
async function playOnDevice(i){if(i==playingIdx){await dev('radio',{i:-1}).catch(()=>{});return status()}
 try{if(saveT)await saveStations();await dev('radio',{i});toast(T('W_THE_PLAYER_STARTED_PLAYING_CONNECTING_AND_BU'));status()}catch(e){toast(e.message,1)}}
async function addStation(name,url){url=url.trim();if(!Conn.up)return toast(T('W_CONNECT_TO_THE_PLAYER_FIRST'),1),false;
 if(!isHttp(url))return toast(T('W_THE_URL_MUST_START_WITH_HTTP_THE_PLAYER_DOES'),1),false;
 if(st.some(s=>s.url==url))return toast(T('W_ALREADY_IN_THE_LIST')),false;
 st.push({name:(name||'').trim()||url.replace(/^http:\/\//,''),url});await saveStations();drawStations();return true}
$('#addtest').onclick=async()=>{let u=$('#addurl').value.trim();if(!u)return;const ok=await preview(u,$('#addtest'));if(ok===null){$('#addmsg').textContent='';return}
 $('#addmsg').textContent=ok?T('W_SOUND_IN_THE_BROWSER_WHETHER_THE_PLAYER_CAN'):T('W_THE_BROWSER_CANNOT_PLAY_IT')};
let addOk='';                                  /* the address the device last said yes to */
$('#addchk').onclick=async()=>{let u=$('#addurl').value.trim();if(!u)return;if(/^https:/i.test(u)){u=u.replace(/^https:/i,'http:');$('#addurl').value=u}
 $('#addmsg').textContent=T('W_CHECKING_ON_THE_PLAYER');$('#addchk').disabled=true;
 try{const r=await probe(u);addOk=r.playable?u:'';$('#addmsg').textContent=r.playable?T('W_THE_PLAYER_CAN_PLAY_IT_0',FMT[r.fmt]||r.fmt):T('W_THE_PLAYER_CANNOT_PLAY_IT')+why(r.why)}catch(e){$('#addmsg').textContent=e.message}finally{$('#addchk').disabled=false}};
$('#addgo').onclick=async()=>{let u=$('#addurl').value.trim();if(!u)return;if(/^https:/i.test(u))u=u.replace(/^https:/i,'http:');
 $('#addmsg').textContent=addOk==u?'':T('W_CHECKING_ON_THE_PLAYER');$('#addgo').disabled=true;
 try{const r=addOk==u?{playable:true,fmt:''}:await probe(u);if(!r.playable){$('#addmsg').textContent=T('W_THE_PLAYER_CANNOT_PLAY_IT')+why(r.why);return}
  if(await addStation($('#addname').value,u)){$('#addname').value='';$('#addurl').value='';$('#addmsg').textContent=T('W_ADDED');addOk=''}}
 catch(e){$('#addmsg').textContent=e.message}finally{$('#addgo').disabled=false}};

/* ================= find: browse radio-browser.info in this browser ================= */
/* radio-browser has no single server: all.api.radio-browser.info is a DNS name for the volunteer servers (de1, de2, nl1, at1, fi1 ...). A request that fails goes to the next server;
   the one that answered last is remembered; the list of servers comes from the API itself (/json/servers) when one answers, else this built-in one. The picker can fix one server. */
const RB_BUILTIN=['all.api.radio-browser.info','de1.api.radio-browser.info','de2.api.radio-browser.info','nl1.api.radio-browser.info','at1.api.radio-browser.info','fi1.api.radio-browser.info'];
const rbOk=h=>/^[a-z0-9-]+(\.[a-z0-9-]+)*\.radio-browser\.info$/i.test(h||'');
let rbHosts=RB_BUILTIN.slice(),rbPick='',rbLast='',rbListed=false;
try{rbLast=localStorage.getItem('rb_last')||'';rbPick=localStorage.getItem('rb_pick')||''}catch{}
if(!rbOk(rbLast))rbLast='';if(!rbOk(rbPick))rbPick='';
if(rbPick&&!rbHosts.includes(rbPick))rbHosts.push(rbPick);   /* a saved pick that is not in the built-in list stays choosable */
function rbSave(k,v){try{localStorage.setItem(k,v)}catch{}}
function rbDraw(){const S=$('#rbsel');if(!S)return;S.replaceChildren(el('option',{value:''},T('W_AUTOMATIC_SWITCH_TO_THE_NEXT_ONE_WHEN_ONE_FA')),...rbHosts.map(h=>el('option',{value:h,selected:h==rbPick},h.replace('.radio-browser.info',''))));S.value=rbPick;
 $('#rbnow').textContent=rbLast?T('W_NOW')+rbLast.replace('.radio-browser.info',''):''}
const fetchJ=async(url,ms)=>{const c=new AbortController(),t=setTimeout(()=>c.abort(),ms);try{const r=await fetch(url,{signal:c.signal});if(!r.ok)throw new Error(T('W_REPLY')+r.status);return await r.json()}finally{clearTimeout(t)}};   /* the timer runs until the body is read too */
async function rbDiscover(host){if(rbListed)return;rbListed=true;
 try{const L=await fetchJ('https://'+host+'/json/servers',6000),n=[...new Set(L.map(x=>String(x.name||'').toLowerCase()).filter(rbOk))];
  if(n.length){rbHosts=['all.api.radio-browser.info',...n.filter(h=>h!='all.api.radio-browser.info')];if(rbPick&&!rbHosts.includes(rbPick))rbHosts.push(rbPick);rbDraw()}}catch{}}
async function rb(path){const order=rbPick?[rbPick]:[...(rbLast?[rbLast]:[]),...rbHosts.filter(h=>h!=rbLast)];let err;
 for(const h of order){try{const j=await fetchJ('https://'+h+'/json/'+path,10000);
   if(h!=rbLast){rbLast=h;rbSave('rb_last',h);rbDraw()}rbDiscover(h);return j}catch(e){err=new Error(h+' '+e.message)}}
 throw new Error(rbPick?T('W_0_IS_UNREACHABLE_YOU_CAN_SET_IT_BACK_TO_AUTO',rbPick):T('W_RADIO_BROWSER_IS_UNREACHABLE_TRIED_0_SERVERS',order.length,err&&err.message||''))}
$('#rbsel').onchange=e=>{rbPick=rbOk(e.target.value)?e.target.value:'';rbSave('rb_pick',rbPick);lists.c=lists.l=lists.tags=null;if(fview=='country'||fview=='lang'||fview=='genre')showView(fview)};
rbDraw();
const GENRES=()=>[['chiptune',T('W_CHIPTUNE')],['video game',T('W_GAME_MUSIC')],['8-bit','8-bit'],['demoscene','Demoscene'],['lofi','Lo-fi'],['chillout',T('W_CHILL')],['ambient',T('W_AMBIENT')],['jazz',T('W_JAZZ')],['classical',T('W_CLASSICAL')],['piano',T('W_PIANO')],['soundtrack',T('W_SOUNDTRACK')],['anime',T('W_ANIME')],['j-pop','J-Pop'],['k-pop','K-Pop'],['pop',T('W_POP')],['rock',T('W_ROCK')],['metal',T('W_METAL')],['electronic',T('W_ELECTRONIC')],['house','House'],['techno','Techno'],['trance','Trance'],['hip hop',T('W_HIP_HOP')],['80s',T('W_80S')],['90s',T('W_90S')],['oldies',T('W_OLDIES')],['blues',T('W_BLUES')],['soul',T('W_SOUL')],['reggae',T('W_REGGAE')],['country',T('W_COUNTRY')],['folk',T('W_FOLK')],['world music',T('W_WORLD_MUSIC')],['news',T('W_NEWS')],['talk',T('W_TALK')]];
const zhLang=c=>{try{return new Intl.DisplayNames([['zh-Hant','zh-Hans','en'][rlang]],{type:'language'}).of(c)}catch{return null}};   /* language names in the radio-search language */
/* region names: one neutral table (tools/gen/i18n/regions.tsv -> regions.js), no flags; the order follows the device's UI language (0 繁中: Taiwan and Hong Kong first, 1 簡中: mainland first, 2 English: by station count) */
let rlangPicked=false,rlang=Lang.cur;   /* until the device says its language (status.lang) */
const rname=cc=>{const r=REGIONS[cc];return r?r[rlang]:cc};   /* a code the table does not know shows as the bare code (no unchecked names) */
function regionOrder(L){if(rlang>1)return L;const k=rlang==0?3:4;return L.map((x,i)=>[x,i]).sort((a,b)=>((REGIONS[a[0].key]||[])[k]??99)-((REGIONS[b[0].key]||[])[k]??99)||a[1]-b[1]).map(x=>x[0])}
let fview='top',fquery=null,foffset=0;const FPAGE=50,lists={};
document.querySelectorAll('#fnav button').forEach(b=>b.onclick=()=>showView(b.dataset.v));
function showView(v){fview=v;document.querySelectorAll('#fnav button').forEach(x=>x.classList.toggle('sel',x.dataset.v==v));const P=$('#fpanel');P.replaceChildren();P.hidden=false;fquery=null;$('#fbar').hidden=true;$('#fres').replaceChildren();$('#fmore').hidden=true;$('#fmsg').textContent='';
 if(v=='top'){P.hidden=true;openList({title:T('W_POPULAR_STATIONS_MOST_LISTENERS'),params:{}});return}
 if(v=='search'){const q=el('input',{className:'grow',placeholder:T('W_STATION_NAME_E_G_CHIPTUNE_ICRT_JAZZ')}),go=el('button',{className:'acc'},T('W_SEARCH'));
  const run=()=>{const t=q.value.trim();if(t)openList({title:T('W_SEARCH_0',t),params:{name:t}})};go.onclick=run;q.onkeydown=e=>{if(e.key=='Enter')run()};P.append(el('div',{className:'row'},q,go));setTimeout(()=>q.focus());return}
 if(v=='genre'){const g=el('div',{className:'row wrap'});GENRES().forEach(([tag,zh])=>g.append(el('button',{className:'sm',on:{click:()=>openList({title:T('W_GENRE_0',zh),params:{tag,tagExact:'true'}})}},zh)));
  const all=el('details',{style:'margin-top:10px'},el('summary',{className:'dim small'},T('W_ALL_TAGS_BY_STATION_COUNT')));const box=el('div',{className:'row wrap',style:'margin-top:8px'});all.append(box);
  all.ontoggle=async()=>{if(!all.open||box.childElementCount)return;box.textContent=T('W_LOADING');try{const L=lists.tags||(lists.tags=await rb('tags?order=stationcount&reverse=true&hidebroken=true&limit=300'));box.textContent='';
   L.filter(t=>t.name&&t.stationcount>=5).forEach(t=>box.append(el('button',{className:'sm',on:{click:()=>openList({title:T('W_TAG_0',t.name),params:{tag:t.name,tagExact:'true'}})}},`${t.name} `,el('span',{className:'dim small'},t.stationcount))))}catch(e){box.textContent=e.message}};
  P.append(g,all);return}
 if(v=='country'||v=='lang'){const f=el('input',{placeholder:v=='country'?T('W_FILTER_E_G_TAIWAN_JAPAN'):T('W_FILTER_E_G_CHINESE_ENGLISH')}),box=el('div',{className:'row wrap',style:'margin-top:8px'},T('W_LOADING'));
  const ls=el('select',{title:T('W_THE_LANGUAGE_USED_FOR_REGION_ORDER_AND_NAMES')},...[[T('W_TEXT'),0],[T('W_TEXT_2'),1],['English',2]].map(([t,i])=>el('option',{value:i,selected:i==rlang},t)));
  P.append(el('div',{className:'row'},f,...(v=='country'?[ls]:[])),box);
  (async()=>{try{let L;if(v=='country'){L=lists.c||(lists.c=await rb('countries?order=stationcount&reverse=true&hidebroken=true'));L=L.filter(c=>c.iso_3166_1).map(c=>({key:c.iso_3166_1,label:'',alt:c.name,n:c.stationcount}));}
   else{L=lists.l||(lists.l=await rb('languages?order=stationcount&reverse=true&hidebroken=true'));L=L.filter(l=>l.name&&l.stationcount>=3).map(l=>({key:l.name,label:(l.iso_639&&zhLang(l.iso_639))||l.name,alt:l.name,n:l.stationcount}))}
   const draw=()=>{const t=f.value.trim().toLowerCase();box.replaceChildren();let M=L;if(v=='country'){L.forEach(x=>{const r=REGIONS[x.key];x.label=rname(x.key);x.all=(r?r.slice(0,3).join(' '):'')});M=regionOrder(L)}
    M.filter(x=>!t||x.label.toLowerCase().includes(t)||x.alt.toLowerCase().includes(t)||(x.all||'').toLowerCase().includes(t)).slice(0,120).forEach(x=>box.append(el('button',{className:'sm',on:{click:()=>openList(v=='country'?{title:x.label,params:{countrycode:x.key}}:{title:T('W_LANGUAGE_0',x.label),params:{language:x.key,languageExact:'true'}})}},x.label+' ',el('span',{className:'dim small'},x.n))))};
   f.oninput=draw;ls.onchange=()=>{rlang=+ls.value;rlangPicked=true;draw()};draw()}catch(e){box.textContent=T('W_COULD_NOT_LOAD_THIS_COMPUTER_MUST_BE_ONLINE')+e.message}})();return}}
async function openList(q,more){if(!more){fquery=q;foffset=0;$('#fres').replaceChildren();$('#ftitle').textContent=q.title;$('#fcount').textContent='';$('#fbar').hidden=false;window.scrollTo({top:$('#fbar').offsetTop-150})}
 $('#fmore').hidden=true;$('#fmsg').textContent=T('W_LOADING');
 const p=new URLSearchParams({limit:FPAGE,offset:foffset,hidebroken:'true',order:'clickcount',reverse:'true',...fquery.params});
 try{const list=await rb('stations/search?'+p);foffset+=list.length;let n=0;
  const cards=[];for(const s of list){const r=result(s);if(r){r.dataset.cn=cnHost(r.dataset.host)?'1':'';cards.push(r)}}
  if(rlang==1)cards.sort((a,b)=>(b.dataset.cn?1:0)-(a.dataset.cn?1:0));   /* 简中: hosts under .cn sort first (no label: the user tests reachability) */
  for(const r of cards){$('#fres').append(r);n++}
  $('#fcount').textContent=T('W_0_STATIONS',$('#fres').childElementCount);$('#fmore').hidden=list.length<FPAGE;
  $('#fmsg').textContent=list.length?T('W_PREVIEW_HERE_LISTEN_ON_THIS_COMPUTER_CHECK_O'):($('#fres').childElementCount?'':T('W_NO_RESULTS'));applyOnly()}
 catch(e){$('#fmsg').textContent=T('W_COULD_NOT_LOAD_THIS_COMPUTER_MUST_BE_ONLINE')+e.message}}
$('#fmore').onclick=()=>openList(fquery,true);
$('#fonly').onchange=applyOnly;
function applyOnly(){const only=$('#fonly').checked;for(const c of $('#fres').children)c.hidden=only&&(c.dataset.state=='bad'||c.dataset.state=='flac'||c.dataset.state=='httpsonly')}
const seenUrls=new Set();
/* a directory entry: the address the device would use; the device's own check decides */
const hostOf=u=>{try{return new URL(u).hostname.toLowerCase()}catch{return''}};
const cnHost=h=>/\.cn$/.test(h||'');
function judge(s){const http=[s.url_resolved,s.url].find(isHttp),any=s.url_resolved||s.url||'',codec=(s.codec||'').trim().toUpperCase();
 return{url:http||any.replace(/^https:/i,'http:'),any,hls:s.hls==1||/\.m3u8?(\?|$)/i.test(http||any),state:/FLAC/.test(codec)?'flac':http?'unchecked':'httpsonly',codec}}
function result(s){const j=judge(s);if(!j.any)return null;
 const chip=el('span',{className:'chip'}),add=el('button',{className:'sm'}),play=el('button',{className:'sm'},T('W_PREVIEW_HERE')),chk=el('button',{className:'sm'},T('W_CHECK_ON_PLAYER'));
 const card=el('div',{className:'card'});card.dataset.host=hostOf(j.any);
 const draw=()=>{const have=st.some(x=>x.url==j.url||(x.alts||[]).includes(j.url));card.dataset.state=j.state;card.style.opacity=j.state=='httpsonly'?'.55':'';chk.disabled=j.state=='checking'||j.state=='flac';chk.textContent=j.state=='ok'||j.state=='bad'?T('W_CHECK_AGAIN'):T('W_CHECK_ON_PLAYER');
  const C={unchecked:[j.hls?T('W_UNCONFIRMED_HLS'):T('W_UNCONFIRMED'),''],httpsonly:[T('W_HTTPS_ONLY_THE_PLAYER_DOES_NOT_SUPPORT_IT'),'warn'],flac:[T('W_FLAC_IS_NOT_SUPPORTED_BY_THE_PLAYER'),'warn'],checking:[T('W_CHECKING_ON_THE_PLAYER'),''],ok:[T('W_THE_PLAYER_CAN_PLAY_IT_0',FMT[j.fmt]||j.fmt||''),'ok'],bad:[T('W_THE_PLAYER_CANNOT_PLAY_IT_2')+why(j.why),'bad']}[j.state];
  chip.textContent=C[0];chip.className='chip '+C[1];
  add.textContent=have?T('W_ALREADY_IN_THE_LIST_2'):T('W_ADD');add.disabled=have||j.state=='bad'||j.state=='flac'||j.state=='checking';add.className='sm'+(!add.disabled&&j.state=='ok'?' acc':'')};
 const check=async()=>{j.state='checking';draw();try{const r=await probe(j.url);j.fmt=r.fmt;j.why=r.why;j.state=r.playable?'ok':'bad'}catch(e){j.state='unchecked';toast(e.message,1)}draw();applyOnly();return j.state=='ok'};
 chk.onclick=check;
 play.onclick=()=>preview(j.any,play);                     /* listening only: the browser follows https, the device does not */
 add.onclick=async()=>{if(j.state!='ok'&&!await check())return toast(T('W_THE_PLAYER_CANNOT_PLAY_IT')+why(j.why),1);if(await addStation(s.name,j.url))toast(T('W_ADDED_0',s.name.trim()));draw()};
 const copy=el('button',{className:'sm',title:T('W_COPY_URL'),on:{click:()=>{navigator.clipboard.writeText(j.any).then(()=>toast(T('W_URL_COPIED')),()=>toast(T('W_COULD_NOT_COPY_PLEASE_SELECT_IT_BY_HAND'),1))}}},T('W_COPY'));
 const icon=/^https:\/\//i.test(s.favicon||'')?el('img',{src:s.favicon,loading:'lazy',referrerPolicy:'no-referrer',style:'width:40px;height:40px;border-radius:8px;object-fit:cover;background:#0a0d11;flex:none',on:{error:e=>e.target.remove()}}):null;
 draw();
 card.append(el('div',{className:'row'},...(icon?[icon]:[]),el('div',{className:'grow',style:'min-width:0'},el('div',{className:'ell'},el('b',{},s.name.trim())),
   el('div',{className:'dim small ell'},[s.codec&&s.codec!='UNKNOWN'?s.codec:T('W_FORMAT_UNKNOWN'),s.bitrate?s.bitrate+' kbps':'',rname(s.countrycode),(s.tags||'').split(',').filter(Boolean).slice(0,3).join(', '),s.clickcount?s.clickcount+T('W_LISTENERS'):''].filter(Boolean).join(' · '))),chip),
  el('div',{className:'url small',title:T('W_CAN_BE_SELECTED_AND_COPIED')},j.any),
  el('div',{className:'row wrap',style:'margin-top:8px'},play,chk,add,el('span',{className:'grow'}),copy));
 return card}
document.querySelector('nav button[data-t="find"]').addEventListener('click',()=>{const v=location.hash.split('/')[1];if(!fquery||v)showView(v||'top')});

/* ================= effects: the rows come from the device's registry (fx_apps), the order is fixed on the device ================= */
const FXU=['','dB','Hz','ms','s','pct',':1','bool','choice'];   /* PC_FXU_* order */
const num=v=>{const s=(+v).toFixed(3).replace(/\.?0+$/,'');return s=='-0'?'0':s};
let fxLoaded=false,fxApps=[],fxEqText='',fxFrom='',fxLiveT=0,fxCards=[];
function slider(label,p,val,onset){
 if(p.unit=='bool'){const c=el('input',{type:'checkbox',checked:val>.5,on:{change:()=>onset(c.checked?1:0)}});return el('div',{className:'sl'},el('span',{},label),el('div',{},c),el('output',{}))}
 if(p.unit=='choice'){const S=el('select',{on:{change:()=>onset(p.lo+S.selectedIndex)}});p.choices.forEach((c,i)=>S.append(el('option',{value:i,selected:p.lo+i==val},c)));return el('div',{className:'sl'},el('span',{},label),S,el('output',{}))}
 const lo=p.lo,hi=p.hi,step=p.step,log=!!(p.flags&1),pct=p.unit=='pct',unit=pct?'%':p.unit,k=pct?100:1;
 const show=x=>log?(+x).toPrecision(3).replace(/\.?0+$/,''):fmt(x*k,pct?1:step)+(unit?' '+unit:'');
 const toP=x=>log?Math.log(x/lo)/Math.log(hi/lo)*1000:x,fromP=q=>log?lo*Math.pow(hi/lo,q/1000):q;
 const out=el('output',{},show(val)+(log&&unit?' '+unit:''));
 const r=el('input',{type:'range',min:log?0:lo,max:log?1000:hi,step:log?1:step,value:toP(Math.min(hi,Math.max(lo,val))),on:{input:()=>{let x=fromP(+r.value);if(!log)x=lo+Math.round((x-lo)/step)*step;out.textContent=show(x)+(log&&unit?' '+unit:'');onset(x)}}});
 return el('div',{className:'sl'},el('span',{},label),r,out)}
const fxF=x=>{const s=num(x);return /[.e]/.test(s)?s:s+'.0'};
/* the device's own text format (format = 1), made from the rows; the equalizer's curve text is carried through as the device exported it */
function fxToml(name){let t='format = 1\n'+(name?`name = ${JSON.stringify(name)}\n`:'');
 const own=a=>a.params.some(p=>p.id=='mix'),prm=a=>a.params.filter(p=>!(p.flags&4)).map(p=>`${p.id} = ${fxF(p.v)}`);
 for(const pass of[0,1,2])for(const a of fxApps){const w=a.id=='freeverb'?2:a.id=='limiter'?1:0;if(w!=pass)continue;t+='\n';
  if(w==2){const lv=a.params.find(p=>p.id=='level'),ps=a.params.filter(p=>p.id!='level'&&!(p.flags&4)).map(p=>`${p.id} = ${fxF(p.v)}`);
   t+=`[[sends]]\nfrom = 0\nlevel = ${fxF(lv?lv.v:0.3)}\n${a.on?'':'on = false\n'}nodes = [{ kind = "${a.id}"${ps.length?', params = { '+ps.join(', ')+' }':''} }]\n`;continue}
  t+=`[[${w?'master':'music'}]]\nkind = "${a.id}"\n${a.on?'':'enabled = false\n'}`;
  if('mix'in a&&!own(a))t+=`mix = ${fxF(a.mix)}\n`;
  if(a.id=='equalizer'){if(fxEqText)t+=fxEqText+'\n';continue}
  const ps=prm(a);if(ps.length)t+='params = { '+ps.join(', ')+' }\n'}
 return t}
function fxLook(a,c){c.card.classList.toggle('off',!a.on);c.card.classList.toggle('dimrow',a.look=='dim');c.why.textContent=a.why||'';c.why.hidden=!a.why}
function fxNow(){const n=fxApps.filter(a=>a.on).length;$('#fxnow').textContent=(n?T('W_0_EFFECT_S_ARE_ON_IN_THE_PLAYER',n):T('W_NO_EFFECTS_ARE_ON_IN_THE_PLAYER'))+(fxFrom?T('W_FROM_0',fxFrom.replace(/\.toml$/,'')):'')}
function fxChanged(){$('#fxtext').value=fxToml();fxNow();
 if($('#fxlive').checked){clearTimeout(fxLiveT);fxLiveT=setTimeout(async()=>{try{await dev('fx_live',{text:fxToml()});const r=await dev('fx_apps');r.apps.forEach((n,i)=>{const a=fxApps[i];if(!a||a.id!=n.id)return;a.look=n.look;a.why=n.why;fxLook(a,fxCards[i])})}catch(e){toast(e.message,1)}},150)}}
function fxRows(){const box=$('#fxnodes');box.replaceChildren();fxCards=[];
 for(const a of fxApps){const card=el('div',{className:'fx'}),why=el('div',{className:'dim small why'}),c={card,why};fxCards.push(c);
  const sw=el('input',{type:'checkbox',checked:a.on,title:T('W_ON_OFF'),on:{change:()=>{a.on=sw.checked;a.look=a.on?'run':'off';a.why='';fxLook(a,c);fxChanged()}}});
  card.append(el('div',{className:'row'},sw,el('h3',{className:'grow'},a.zh),a.cls=='heavy'?el('span',{className:'chip'},T('W_CPU_HEAVY')):null),el('div',{className:'dim small'},a.desc_zh),why);
  if(a.id=='equalizer')card.append(el('div',{className:'dim small'},T('W_TO_CHANGE_THE_CURVE_PASTE_AUTOEQ_TEXT_UNDER')));
  else{
   if('mix'in a&&!a.params.some(p=>p.id=='mix'))card.append(slider(T('W_DEPTH'),{lo:0,hi:1,step:.01,unit:'',flags:0},a.mix,v=>{a.mix=v;fxChanged()}));
   for(const p of a.params){if(p.flags&4)continue;
    const q={...p,unit:FXU[p.unit]||'',choices:p.choices_zh};
    card.append(slider(p.zh,q,p.v,v=>{p.v=v;fxChanged()}))}}
  fxLook(a,c);box.append(card)}}
async function fxLoadApps(){const r=await dev('fx_apps');fxApps=r.apps;fxRows();$('#fxbody').hidden=false;fxNow()}
async function loadFx(){try{const f=await dev('fx_list');fxFrom=f.state_from||'';const S=$('#fxsel');S.replaceChildren(el('option',{value:''},T('W_IMPORT_FROM_FILE')));
 f.presets.forEach(p=>S.append(el('option',{value:p},p.replace(/\.toml$/,''))));
 const e=await dev('fx_export');const m=e.text.match(/equalizer = """[\s\S]*?"""/);fxEqText=m?m[0]:'';$('#fxtext').value=e.text;
 await fxLoadApps();fxLoaded=true}catch(e){toast(e.message,1)}}
$('#fxparse').onclick=async()=>{try{const r=await dev('fx_live',{text:$('#fxtext').value});const m=$('#fxtext').value.match(/equalizer = """[\s\S]*?"""/);if(m)fxEqText=m[0];
 const t=$('#fxtext').value;await fxLoadApps();$('#fxtext').value=t;toast(r.unsupported&&r.unsupported.length?T('W_TEXT_APPLIED_NOT_SUPPORTED_BY_THE_PLAYER')+r.unsupported.join(T('W_TEXT_3')):T('W_TEXT_APPLIED'))}catch(e){toast(e.message,1)}};
$('#fxuse').onclick=async()=>{const n=$('#fxsel').value;if(!n)return toast(T('W_CHOOSE_A_FILE_TO_IMPORT_ON_THE_LEFT_FIRST'),1);
 try{await dev('fx_select',{name:n});toast(T('W_THE_PLAYER_NOW_USES')+n.replace(/\.toml$/,''));fxLoaded=false;await loadFx();status()}catch(e){toast(e.message,1)}};
async function saveAs(file){try{await dev('fx_save',{name:file,text:fxToml(file.replace(/\.toml$/,''))});toast(T('W_SAVED_THE_PLAYER_IS_USING')+file.replace(/\.toml$/,''));fxLoaded=false;await loadFx();status()}catch(e){toast(T('W_SAVE_FAILED')+e.message,1)}}
$('#fxsave').onclick=()=>fxFrom?saveAs(fxFrom):$('#fxsaveas').click();
$('#fxsaveas').onclick=()=>{let f=prompt(T('W_FILE_NAME_LETTERS_DIGITS'),(fxFrom||'my').replace(/\.toml$/,'').replace(/[^\w-]+/g,'_')||'my');if(!f)return;f=f.replace(/[^\w-]+/g,'_');saveAs(f.endsWith('.toml')?f:f+'.toml')};

/* ================= device settings: choices, applied at once ================= */
const pct=(a,b,s)=>{const o=[];for(let v=a;v<=b;v+=s)o.push([v,v+'%']);return o};
const mkSET=()=>({
 brightness:{g:'screen',label:T('W_BRIGHTNESS'),opts:pct(10,100,10)},
 haptic_mv:{g:'screen',label:T('W_HAPTIC_STRENGTH'),hint:T('W_FEEDBACK_WHEN_TAPPING_MENUS_THE_PLAYER_S_HAP'),opts:[[640,T('W_VERY_WEAK')],[850,T('W_WEAK')],[1070,T('W_FAIRLY_WEAK')],[1280,T('W_NORMAL')],[1490,T('W_FAIRLY_STRONG')],[1700,T('W_STRONG')],[1920,T('W_STRONGEST')]]},
 haptic_ms:{g:'screen',label:T('W_HAPTIC_LENGTH'),opts:[[5,T('W_VERY_SHORT_5_MS')],[8,T('W_SHORT_8_MS_DEFAULT')],[10,'10 ms'],[15,'15 ms'],[20,'20 ms'],[30,T('W_LONG_30_MS')],[0,T('W_FULL')]]},
 home_hold_ms:{g:'screen',label:T('W_HOME_HOLD'),hint:T('W_HOME_HOLD_HINT'),opts:[[2000,T('W_HOME_HOLD_20')],[3000,T('W_HOME_HOLD_30')],[4000,T('W_HOME_HOLD_40')]]},
 cover_smooth:{g:'screen',label:T('W_COVER_ROTATION'),opts:[[0,T('W_STEP_BY_STEP_SAVES_POWER')],[1,T('W_SMOOTH')]]},
 rg:{g:'sound',label:T('W_REPLAYGAIN_VOLUME'),hint:T('W_ADJUSTS_EACH_TRACK_S_VOLUME_FROM_ITS_REPLAYG'),opts:[[1,T('W_TRACK_DEFAULT')],[2,T('W_ALBUM')],[0,T('W_OFF_2')]]},
 rg_untag:{g:'sound',label:T('W_FILES_WITHOUT_TAGS'),hint:T('W_WHEN_REPLAYGAIN_IS_ON_FILES_WITHOUT_TAGS_USE'),opts:Array.from({length:19},(_,i)=>[6-i,i==6?T('W_0_DB_DEFAULT_UNCHANGED'):(6-i>0?'+':'')+(6-i)+' dB'])},
 toysfx:{g:'sound',label:T('W_TOY_SOUNDS'),hint:T('W_PROMPT_SOUNDS_OF_BUBBLE_WRAP_SLOT_MACHINE_MO'),opts:[[1,T('W_ON_DEFAULT')],[0,T('W_OFF')]]},
 cover_auto:{g:'screen',label:T('W_COVER_CACHE'),hint:T('W_AUTO_WHILE_STOPPED_AND_WHILE_PLAYING_48_KHZ'),opts:[[1,T('W_AUTO_DEFAULT')],[0,T('W_MANUAL')]]},
 lang:{g:'screen',label:T('W_LANGUAGE_2'),hint:T('W_THE_LANGUAGE_OF_THE_PLAYER_S_SCREEN_TAKES_EF'),opts:[[0,T('W_DEFAULT')],[1,T('W_TEXT_2')],[2,'English']]},
 volume_max:{g:'sound',label:T('W_VOLUME_LIMIT'),hint:T('W_THE_KNOB_CANNOT_TURN_HIGHER_THAN_THIS_PROTEC'),opts:Array.from({length:17},(_,i)=>[100-5*i,i?String(100-5*i):T('W_NO_LIMIT_DEFAULT')])},
 bt_mode:{g:'bt',label:T('W_BLUETOOTH_MODE'),hint:T('W_THE_PLAYER_REBOOTS_WHEN_YOU_SWITCH'),opts:[[0,T('W_OFF_2')],[1,T('W_SPEAKER_PHONE_PLAYER_3_5_MM')],[2,T('W_HEADPHONES_PLAYER_BLUETOOTH_HEADPHONES')],[3,T('W_AURACAST_BROADCAST')],[4,T('W_AURACAST_LISTEN_SOMEONE_ELSE_S_BROADCAST_PLA')]]},
 hp_absvol:{g:'bt',label:T('W_HEADPHONE_VOLUME_SYNC'),hint:T('W_WHEN_OFF_VOLUME_IS_SET_ON_THE_PLAYER_AGAIN_A'),opts:[[1,T('W_ON')],[0,T('W_OFF')]]},
 cast_q:{g:'bt',reb:T('W_SAVED_TAKES_EFFECT_THE_NEXT_TIME_A_BROADCAST'),label:T('W_BROADCAST_QUALITY'),hint:T('W_ONLY_USEFUL_FOR_AURACAST_BROADCAST'),opts:[[0,T('W_24_KHZ_STEREO')],[1,T('W_48_KHZ_STEREO_DEFAULT')],[2,T('W_48_KHZ_HIGH_BITRATE_RECEIVERS_MAY_NOT_BE_ABL')]]},

 album_sec:{g:'screen',label:T('W_PHOTO_CHANGE_INTERVAL'),hint:T('W_HOW_LONG_EACH_PHOTO_STAYS_ON_THE_PHOTO_WALL'),opts:[[10,T('W_10_SECONDS')],[30,T('W_30_SECONDS_DEFAULT')],[60,T('W_1_MINUTE')],[300,T('W_5_MINUTES')]]},
 album_fade:{g:'screen',label:T('W_PHOTO_CHANGE_STYLE'),opts:[[1,T('W_FADE_IN_DEFAULT')],[0,T('W_CUT')]]},
 vocab_new:{g:'tools',label:T('W_NEW_WORDS_PER_DAY'),hint:T('W_HOW_MANY_NEW_WORDS_APPEAR_EACH_DAY_TAKES_EFF'),opts:[[5,'5'],[10,T('W_10_DEFAULT')],[15,'15'],[20,'20'],[30,'30'],[50,'50']]},
 gapless:{g:'adv',label:T('W_GAPLESS_PLAYBACK'),hint:T('W_ADJACENT_TRACKS_OF_THE_SAME_FORMAT_AND_SAMPL'),opts:[[1,T('W_ON_DEFAULT')],[0,T('W_OFF')]]},
 dac_bits:{g:'adv',label:T('W_WIRED_BIT_DEPTH'),hint:T('W_32_BIT_DEFAULT_16_BIT_IS_THE_UNDITHERED_REFE'),opts:[[32,T('W_32_BIT_DEFAULT')],[16,T('W_16_BIT')]]},
 dac_hard_rate:{fp:1,g:'adv',label:T('W_WIRED_HARD_RATE_LIMIT'),hint:T('W_A_SOURCE_ABOVE_THIS_RATE_IS_REFUSED_BEFORE_IT'),opts:[[384000,T('W_384K_DEFAULT')],[192000,'192 kHz'],[96000,'96 kHz'],[48000,'48 kHz']]},
 dac_hpf:{fp:1,g:'adv',label:T('W_DAC_HIGH_PASS_FILTER'),hint:T('W_ON_DEFAULT_THE_ONLY_FILTER_STAGE_THAT_TOUCHES'),opts:[[1,T('W_ON_DEFAULT')],[0,T('W_OFF')]]},
 dac_fs:{fp:1,g:'adv',label:T('W_DAC_FULL_SCALE_OUTPUT'),hint:T('W_FULL_SCALE_AT_THE_TOP_VOLUME_STEP_1_0_VRMS_GI'),opts:[[0,T('W_1_0_VRMS_DEFAULT')],[1,T('W_1_41_VRMS')]]},
 dac_amute:{fp:1,g:'adv',label:T('W_DAC_AUTO_MUTE'),hint:T('W_ON_DEFAULT_THE_CHIP_MUTES_AFTER_8192_SILENT_S'),opts:[[1,T('W_ON_DEFAULT')],[0,T('W_OFF')]]},
 bt_codec:{g:'bt',label:T('W_BT_CODEC_LIMIT'),hint:T('W_BT_CODEC_LIMIT_HINT'),opts:[[3,'LDAC'],[2,'aptX'],[1,'AAC'],[0,'SBC']]},
 bt_rate:{g:'bt',label:T('W_BT_RATE_LIMIT'),hint:T('W_BT_RATE_LIMIT_HINT'),opts:[[96000,'96 kHz'],[48000,'48 kHz'],[44100,'44.1 kHz']]},
 bt_bits:{g:'bt',label:T('W_BT_BITS_LIMIT'),hint:T('W_BT_BITS_LIMIT_HINT'),opts:[[24,'24-bit'],[16,'16-bit']]},
 bt_chan:{g:'bt',label:T('W_BT_CHANNEL_MODE'),hint:T('W_BT_CHANNEL_MODE_HINT'),opts:[[0,T('W_STEREO_MODE')],[1,T('W_JOINT_STEREO_MODE')]]},
 dac_slot24_from:{fp:1,g:'adv',label:T('W_DAC_24_BIT_SLOT_FROM'),hint:T('W_DAC_24_BIT_SLOT_FROM_HINT'),opts:[[0,T('W_OFF_DEFAULT')],[1,T('W_352_8K_AND_UP')],[2,T('W_192K_AND_UP')],[3,T('W_EVERY_RATE')]]},
 ldac_kbps:{g:'adv',label:T('W_LDAC_TOP_BITRATE'),hint:T('W_ONLY_USEFUL_ON_FIRMWARE_WITH_LDAC_IN_HEADPHO'),opts:[[990,T('W_990_KBPS_DEFAULT')],[660,'660 kbps'],[492,'492 kbps'],[330,'330 kbps']]},
 cstep:{g:'adv',label:T('W_CONNECTION_STEPPING'),hint:T('W_CONNECTION_PARAMETERS_ARE_TRIED_STEP_BY_STEP'),opts:[[1,T('W_ON_DEFAULT')],[0,T('W_OFF')]]},
 exp_fmt:{g:'adv',label:T('W_EXPERIMENTAL_FORMATS'),hint:T('W_EXPERIMENTAL_FORMATS_HINT'),opts:[[0,T('W_OFF_DEFAULT')],[1,T('W_ON')]]},
 cpu_mhz:{g:'adv',label:T('W_CPU_CLOCK'),hint:T('W_CPU_CLOCK_HINT'),opts:[[320,T('W_320_MHZ_DEFAULT')],[240,'240 MHz'],[160,'160 MHz']]},
 amiga_mix:{g:'adv',label:T('W_AMIGA_TONE'),hint:T('W_AMIGA_TONE_HINT'),opts:[[0,T('W_OFF_DEFAULT')],[1,'A500'],[2,'A500 + LED'],[3,'A1200']]},
 rotate:{g:'screen',label:T('W_SCREEN_ROTATION'),hint:T('W_ROTATION_HINT'),opts:[[0,'0°'],[1,'180°']]},
 splash:{g:'screen',label:T('W_BOOT_PICTURE'),hint:T('W_BOOT_PICTURE_HINT'),opts:[[1,T('W_ON_DEFAULT')],[0,T('W_OFF')]]},
 font_src:{g:'adv',label:T('W_FONT_SOURCE'),hint:T('W_AUTO_THE_TF_CARD_S_FONTS_FIRST_ELSE_THE_BUIL'),opts:[[0,T('W_AUTO_DEFAULT')],[1,T('W_BUILT_IN_ONLY')],[2,T('W_TF_CARD_ONLY')]]},
});
let SET=mkSET();
let cfgLoaded=false;
async function loadClock(){try{const c=await dev('clock_get');const a=c.alarm;$('#al-on').value=a.on?1:0;$('#al-time').value=String(Math.floor(a.min/60)).padStart(2,'0')+':'+String(a.min%60).padStart(2,'0');
 if(![...$('#al-days').options].some(o=>+o.value==a.days))$('#al-days').append(el('option',{value:a.days},T('W_CUSTOM')));$('#al-days').value=a.days;
 $('#al-sound').textContent=a.sound?a.sound.split('/').pop():T('W_BUILT_IN_CHIME');$('#al-builtin').disabled=!a.sound;
 $('#tz-cur').textContent=tzLabel(c.tz,c.tz_min);
 $('#sun-cur').textContent=c.sun?T('W_LOCATION_SET')+(c.sun_lat<0?T('W_S'):T('W_N'))+Math.abs(c.sun_lat/100).toFixed(2)+T('W_TEXT_4')+(c.sun_lon<0?T('W_W'):T('W_E'))+Math.abs(c.sun_lon/100).toFixed(2)+'°':T('W_NO_LOCATION_SET_THE_PLAYER_S_CLOCK_PAGE_AND');
 $('#sun-clear').hidden=!c.sun;$('#sun-set').textContent=c.sun?T('W_SET_THE_LOCATION_AGAIN'):T('W_SET_THE_SUNRISE_LOCATION');
 $('#al-note').textContent=c.synced?'':T('W_THE_PLAYER_HAS_NOT_GOT_THE_TIME_YET_THE_ALAR')}catch(e){toast(e.message,1)}}
async function setClock(args,msg){try{await dev('clock_set',args);toast(msg||T('W_APPLIED'));loadClock()}catch(e){toast(e.message,1)}}
$('#al-on').onchange=()=>setClock({alarm_on:$('#al-on').value=='1'},$('#al-on').value=='1'?T('W_ALARM_ON'):T('W_ALARM_OFF'));
$('#al-time').onchange=()=>{const[h,m]=$('#al-time').value.split(':').map(Number);if(!isNaN(h))setClock({alarm_min:h*60+m},T('W_ALARM_TIME_CHANGED'))};
$('#al-days').onchange=()=>setClock({alarm_days:+$('#al-days').value});
/* the browser's IANA zone -> a POSIX TZ string with the daylight-saving rules, computed here from the zone's real transitions (the device has no zone database) */
const tzFmt={};
function tzOff(zone,ms){const f=tzFmt[zone]||(tzFmt[zone]=new Intl.DateTimeFormat('en-US',{timeZone:zone,hourCycle:'h23',year:'numeric',month:'numeric',day:'numeric',hour:'numeric',minute:'numeric',second:'numeric'}));
 const p={};for(const x of f.formatToParts(new Date(ms)))p[x.type]=+x.value;
 return Math.round((Date.UTC(p.year,p.month-1,p.day,p.hour%24,p.minute,p.second)-Math.floor(ms/1000)*1000)/60000)}   /* minutes east of UTC */
const tzP=m=>{const a=Math.abs(m),h=Math.floor(a/60),mm=a%60;return(m>0?'-':'')+h+(mm?':'+String(mm).padStart(2,'0'):'')};   /* POSIX offset: east is negative */
const tzN=m=>'<'+(m<0?'-':'+')+String(Math.floor(Math.abs(m)/60)).padStart(2,'0')+(Math.abs(m)%60?String(Math.abs(m)%60).padStart(2,'0'):'')+'>';
function tzTrans(zone,yr){const out=[];let t=Date.UTC(yr,0,1),o=tzOff(zone,t);const end=Date.UTC(yr+1,0,1);
 while(t<end){const n=t+864e5,on=tzOff(zone,n);if(on!=o){let lo=t,hi=n;while(hi-lo>6e4){const mid=Math.floor((lo+hi)/2/6e4)*6e4;if(mid<=lo){break}if(tzOff(zone,mid)==o)lo=mid;else hi=mid}out.push({t:hi,from:o,to:on});o=on}t=n}
 return out}
function tzRuleAt(yr,m,w,d,sec,off){const first=Date.UTC(yr,m-1,1),wd1=new Date(first).getUTCDay(),dim=new Date(Date.UTC(yr,m,0)).getUTCDate();
 let day=1+((d-wd1)%7+7)%7+(w-1)*7;while(day>dim)day-=7;return Date.UTC(yr,m-1,day)+sec*1000-off*6e4}
function tzFromZone(zone,now=Date.now()){
 const y=new Date(now).getUTCFullYear(),o0=tzOff(zone,now);
 const fixed=(why)=>({posix:tzN(o0)+tzP(o0),exact:!why,why:why||'',dst:false});
 const ys=[];for(let i=0;i<6;i++)ys.push(tzTrans(zone,y+i));const a=ys[0],b=ys[1];   /* six years: a rule that only fits two of them is no rule */
 if(!a.length&&!b.length)return fixed();
 if(a.length!=2||b.length!=2||a[0].from!=a[1].to||a[0].to!=a[1].from)return fixed(T('W_THE_DAYLIGHT_SAVING_RULE_OF_THIS_TIME_ZONE_C'));
 const up=a.find(x=>x.to>x.from),dn=a.find(x=>x.to<x.from),std=up.from,dst=up.to;
 const mk=(tr,off)=>{const base=tr.t+off*6e4;   /* the wall clock at the transition; a rule may also say "Saturday 24:00" (/24) or "Sunday -1:00" (/-1) for the same instant, so the day before and the day after are tried too */
  for(const k of[0,1,-1]){const l=new Date(base-k*864e5),m=l.getUTCMonth()+1,day=l.getUTCDate(),dim=new Date(Date.UTC(l.getUTCFullYear(),m,0)).getUTCDate(),
    sec=new Date(base).getUTCHours()*3600+new Date(base).getUTCMinutes()*60+new Date(base).getUTCSeconds()+k*86400,wd=l.getUTCDay(),cands=day+7>dim?[5,Math.ceil(day/7)]:[Math.ceil(day/7)];   /* the last week first: most rules say "last Sunday" */
   for(const w of cands){let ok=true;for(let yr=y;yr<y+6;yr++){const tt=ys[yr-y].find(x=>(x.to>x.from)==(tr.to>tr.from));if(!tt||tzRuleAt(yr,m,w,wd,sec,off)!=tt.t){ok=false;break}}
    if(ok)return{m,w,d:wd,sec}}}return null};
 const r1=mk(up,std),r2=mk(dn,dst);if(!r1||!r2)return fixed(T('W_THE_DAYLIGHT_SAVING_RULE_OF_THIS_TIME_ZONE_C'));
 const hm=sec=>{const a=Math.abs(sec);return(sec<0?'-':'')+Math.floor(a/3600)+(a%3600?':'+String(Math.floor(a%3600/60)).padStart(2,'0'):'')};
 const rs=r=>'M'+r.m+'.'+r.w+'.'+r.d+(r.sec==7200?'':'/'+hm(r.sec));
 return{posix:tzN(std)+tzP(std)+tzN(dst)+(dst==std+60?'':tzP(dst))+','+rs(r1)+','+rs(r2),exact:true,why:'',dst:true}}
const tzLabel=(tz,min)=>{const m=Math.round(min||0),a=Math.abs(m);return 'UTC'+(m<0?'-':'+')+Math.floor(a/60)+(a%60?':'+String(a%60).padStart(2,'0'):'')+(/,M/.test(tz||'')?T('W_WITH_DAYLIGHT_SAVING'):'')};
$('#tz-browser').onclick=()=>{let z;try{z=Intl.DateTimeFormat().resolvedOptions().timeZone;const r=tzFromZone(z);
  setClock({tz:r.posix},r.exact?T('W_USING_THE_BROWSER_S_TIME_ZONE')+z:r.why)}catch(e){toast(T('W_THIS_BROWSER_GIVES_NO_TIME_ZONE')+e.message,1)}};
{const sel=$('#tz-fixed');sel.append(el('option',{value:''},T('W_PICK_AN_OFFSET_BY_HAND')));
 for(const m of[-720,-660,-600,-570,-540,-480,-420,-360,-300,-240,-210,-180,-120,-60,0,60,120,180,210,240,270,300,330,345,360,390,420,480,525,540,570,600,630,660,720,765,780,840])sel.append(el('option',{value:m},tzLabel('',m)));
 sel.onchange=()=>{if(sel.value==='')return;const m=+sel.value;sel.value='';setClock({tz:tzN(m)+tzP(m)},T('W_TIME_ZONE_SET')+tzLabel('',m))}}
$('#sun-set').onclick=()=>{$('#sun-guide').hidden=false};
$('#sun-cancel').onclick=()=>{$('#sun-guide').hidden=true};
$('#sun-go').onclick=()=>{if(!navigator.geolocation){toast(T('W_THIS_BROWSER_CANNOT_GET_THE_LOCATION'),1);return}
 navigator.geolocation.getCurrentPosition(p=>{$('#sun-guide').hidden=true;setClock({sun_lat:Math.round(p.coords.latitude*100),sun_lon:Math.round(p.coords.longitude*100)},T('W_LOCATION_SAVED_ON_THE_PLAYER'))},
  e=>toast(e.code==1?T('W_YOU_DID_NOT_ALLOW_THE_LOCATION_SO_NOTHING_WA'):T('W_COULD_NOT_GET_THE_LOCATION')+e.message,1),{timeout:15000,maximumAge:600000})};
$('#sun-clear').onclick=()=>setClock({sun_clear:true},T('W_LOCATION_CLEARED'));
$('#al-builtin').onclick=()=>setClock({sound:''},T('W_RINGTONE_IS_BACK_TO_THE_BUILT_IN_CHIME'));
async function loadCfg(){loadClock();try{const c=(await dev('config_get')).config;for(const g of['screen','sound','bt','tools','adv'])$('#g-'+g).replaceChildren();
 for(const[k,m]of Object.entries(SET)){const o=c[k];if(!o)continue;if(m.fp&&!(lastStatus&&lastStatus.board==='fakepod'))continue;   /* the FakePod's DAC options: not shown on the round board, where they do nothing */const cur=o.value;const s=el('select',{});let found=false;
  for(const[v,t]of m.opts){s.append(el('option',{value:v,selected:v==cur},t));if(v==cur)found=true}
  if(!found)s.prepend(el('option',{value:cur,selected:true},T('W_NOW_0',cur)));
  s.onchange=()=>applySet(k,+s.value,m,o,s);
  $('#g-'+m.g).append(el('div',{className:'set'},el('div',{},el('div',{},m.label),m.hint?el('div',{className:'dim small'},m.hint):null),s))}
 cfgLoaded=true}catch(e){toast(e.message,1)}}
async function applySet(k,v,m,o,s){
 if(k=='bt_mode'&&!confirm(T('W_SWITCHING_THE_BLUETOOTH_MODE_NEEDS_A_REBOOT'))){s.value=o.value;return}
 try{const r=await dev('config_set',{values:{[k]:v}});o.value=v;
  if(k=='bt_mode'){await dev('reboot').catch(()=>{});toast(T('W_REBOOTING_PRESS_CONNECT_AGAIN_AFTERWARDS'));return}
  toast(r.reboot?(m.reb||T('W_SAVED_TAKES_EFFECT_AFTER_A_REBOOT')):T('W_APPLIED'))}catch(e){s.value=o.value;toast(e.message,1)}}
$('#wsave').onclick=async()=>{const ssid=$('#ssid').value.trim(),pass=$('#pass').value;if(!ssid)return toast(T('W_ENTER_THE_NETWORK_NAME'),1);
 try{await dev('wifi_set',{ssid,pass});$('#pass').value='';toast(T('W_WRITTEN_THE_NEXT_TIME_YOU_LISTEN_TO_RADIO_IT'))}catch(e){toast(e.message,1)}};
$('#reboot').onclick=async()=>{if(!confirm(T('W_REBOOT_WHATEVER_IS_PLAYING_KEEPS_PLAYING')))return;await dev('reboot').catch(()=>{});toast(T('W_REBOOTING_ABOUT_10_S_THEN_PRESS_CONNECT_AGAI'))};
drawStations();

/* ?demo: a pretend device, to look at the page without one */
/* the "choose a file" buttons open the hidden file input after them (no inline handlers: the CSP forbids them) */
document.addEventListener('click',e=>{const b=e.target.closest('[data-pick]');if(b&&b.nextElementSibling)b.nextElementSibling.click()});

/* Web Serial exists in Chrome and Edge only (not Firefox, not Safari): say so at the top, the rest of the page (radio search, preview, lists, fonts to download) still works */
$('#nobrowser').hidden=Conn.supported;

/* ================= language: the menu, and what to draw again when it changes ================= */
{const sel=$('#langsel');sel.replaceChildren(...LANG_NAMES.map((n,i)=>el('option',{value:i},n)));sel.onchange=()=>Lang.set(+sel.value)}
function langChanged(){rlang=Lang.cur;rlangPicked=false;SET=mkSET();setConnected(Conn.up);drawListSel();drawStations();rbDraw();
 try{if(lm)listDrawEditor()}catch(e){}loadDeviceLists();
 const t=document.querySelector('nav button.sel').dataset.t;fxLoaded=false;cfgLoaded=false;if(Conn.up){status();if(t=='fx')loadFx();if(t=='dev')loadCfg()}
 if(t=='find'&&typeof showView=='function')showView(fview)}
Lang.onChange(langChanged);
setConnected(false);drawStations();loadDeviceLists();Lang.apply();

