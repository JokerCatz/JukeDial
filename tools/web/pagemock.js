// SPDX-License-Identifier: LGPL-2.1-or-later
/* The pretend device and its tests for webcheck (tools/web/check.sh): loaded by boot.js only when the address has ?mockserial or ?demo, never shipped by web_publish.py.
   It runs after every other script and uses the page's globals (answer, connect, dev, Conn, flashTab, fontTab ...). */
/* cjk-allow-begin: the pretend device and its tests (webcheck) answer and assert in 繁中 */
if(location.search.includes('demo')||location.search.includes('mockserial')){
 const D={volume:48,stations:[{name:'Retro Lofi Chiptune Radio',url:'http://a5.asurahosting.com:8150/radio.mp3'},{name:'Radio GamePlay',url:'http://c22.radioboss.fm:8144/GamePlay'},{name:'Kaaos Chiptune',url:'http://stream.kaaosradio.fi:8000/chip'}],
  presets:['mixed.toml','warm.toml'],apps:[],cfg:{brightness:60,volume_max:100,haptic_mv:1280,haptic_ms:8,home_hold_ms:3000,cover_smooth:0,cast_q:1,bt_mode:0,hp_absvol:1,rg:1,rg_untag:0,toysfx:1,splash:1,cover_auto:1,album_sec:30,album_fade:1,vocab_new:10,gapless:1,dac_bits:32,dac_hard_rate:384000,dac_hpf:1,dac_fs:0,dac_amute:1,bt_codec:3,bt_rate:96000,bt_bits:24,bt_chan:0,dac_slot24_from:0,ldac_kbps:990,cstep:1,font_src:0,lang:0,rotate:0,exp_fmt:0,amiga_mix:0},radio:1};
 const T={'mixed.toml':'format = 1\nname = "Mixed"\n\n[[music]]\nkind = "tape"\n\n[[music]]\nkind = "crossfeed"\n\n[[music]]\nkind = "compressor"\nparams = { threshold_db = -20, ratio = 3 }\n\n[[music]]\nkind = "tone"\nmix = 0.5\nparams = { warmth = 0.4 }\n\n[[master]]\nkind = "limiter"\n\n[[sends]]\nfrom = 1\nlevel = 0.3\nnodes = [{ kind = "freeverb" }]\n'};
 const P=(id,zh,lo,hi,def,step,unit,flags=0,extra={})=>({id,zh,en:id,lo,hi,def,step,unit,flags,v:def,...extra});
 D.apps=[{id:'gain',zh:'音量增益',desc_zh:'整體調大或調小。',cls:'light',look:'off',why:'',on:false,params:[P('gain_db','增益',-24,12,0,.5,1)]},
  {id:'equalizer',zh:'等化器',desc_zh:'耳機校正曲線。',cls:'light',look:'off',why:'',on:false,mix:1,params:[P('preamp_db','前級',-24,12,0,.5,1)]},
  {id:'tone',zh:'音色',desc_zh:'用形容詞調整聲音個性。',cls:'light',look:'off',why:'',on:false,mix:1,params:[P('warmth','溫暖',-1.5,1.5,0,.05,0)]},
  {id:'compressor',zh:'壓縮',desc_zh:'縮小大小聲差距。',cls:'light',look:'off',why:'',on:false,mix:1,params:[P('threshold_db','門檻',-60,0,-24,1,1),P('ratio','比例',1,20,2.5,.1,6)]},
  {id:'freeverb',zh:'殘響',desc_zh:'加上空間感。',cls:'heavy',look:'dim',why:'電池供電時暫停',on:true,params:[P('level','送出量',0,1,.3,.01,5),P('room','空間大小',0,1,.5,.01,0)]},
  {id:'limiter',zh:'限幅',desc_zh:'防止爆音。',cls:'light',look:'run',why:'',on:true,params:[P('ceiling_db','上限',-12,0,-1,.1,1)]}];
 const EQ='[[music]]\nkind = "equalizer"\nenabled = false\nequalizer = """\nPreamp: 0.0 dB\n"""\n';
 const exportText=()=>'format = 1\n\n'+EQ+'\n[[master]]\nkind = "limiter"\n';
 const answer=async(op,a={})=>{switch(op){case'status':return{lang:D.cfg.lang,board:'jc3636f118',playing:D.radio>=0?D.stations[D.radio].name+'\nPower Blade 2 - Jacquelin (NES)':'',radio:D.radio,volume:D.volume,wifi:true,rssi:-62,internal_free:250000,internal_min:130000,psram_free:15e6};
  case'stations_get':return{stations:D.stations,lists:['radio','cn'],list:D.list||'radio'};case'stations_put':D.stations=a.stations;D.putList=a.list;return{stations:a.stations.length};case'radio_list':D.list=a.list;return{stations:a.list=='cn'?[]:D.stations,lists:['radio','cn'],list:a.list};case'radio':D.radio=a.i;return{};case'stop':D.radio=-1;return{};
  case'fx_list':return{presets:D.presets,state_from:D.from||''};case'fx_file':return{text:T[a.name]||'format = 1\n'};
  case'fx_export':D.exported=true;return{text:exportText()};case'fx_apps':return{apps:JSON.parse(JSON.stringify(D.apps))};
  case'fx_live':D.live=a.text;return{unsupported:[]};case'fx_select':D.selected=a.name;D.from=a.name;D.apps.find(x=>x.id=='tone').on=true;return{};case'fx_save':D.saved=a.name;return{};case'config_get':{const c={};for(const[k,v]of Object.entries(D.cfg))c[k]={value:v};return{config:c}}
  case'config_set':(D.setCalls||(D.setCalls=[])).push(a.values);Object.assign(D.cfg,a.values);return{changed:1};
  case'clock_get':return{alarm:D.alarm||(D.alarm={on:false,min:420,days:62,sound:''}),tz:D.tz||'UTC-8',tz_min:D.tz?-300:480,synced:true,sun:!!D.sun,...(D.sun||{})};
  case'clock_set':(D.clockSets||(D.clockSets=[])).push(a);if('tz'in a)D.tz=a.tz;if('sun_lat'in a)D.sun={sun_lat:a.sun_lat,sun_lon:a.sun_lon};if(a.sun_clear)D.sun=null;if(D.alarm){if('alarm_on'in a)D.alarm.on=a.alarm_on;if('alarm_min'in a)D.alarm.min=a.alarm_min;if('alarm_days'in a)D.alarm.days=a.alarm_days}return{};
  case'probe':await new Promise(r=>setTimeout(r,800));return isHttp(a.url)&&!/zelerk/.test(a.url)?{playable:true,fmt:'mp3',why:''}:{playable:false,fmt:'',why:'https'};case'list_ls':return{lists:Object.entries(D.lists||{}).map(([name,z])=>({name,size:z.length}))};
  case'list_put_begin':D.up={name:a.name,size:a.size,parts:[],got:0};return{chunk:2048};
  case'list_put':{const b=Uint8Array.from(atob(a.data),c=>c.charCodeAt(0));if(a.off+b.length<=D.up.got&&a.off!=D.up.got)return{got:D.up.got};if(a.off!=D.up.got)throw new Error('out of order');D.up.parts.push(b);D.up.got+=b.length;return{got:D.up.got}}
  case'list_put_stat':return{got:D.up.got};
  case'list_put_end':{const z=new Uint8Array(D.up.got);let o=0;for(const p of D.up.parts){z.set(p,o);o+=p.length}if(o!=D.up.size||GlyphPack.crc32(z)!=a.crc)throw new Error('CRC');
   const f=await GlyphPack.unzip(z);if(!f.has('pack.bin')||!f.has('source.json'))throw new Error('not a pack');(D.lists||(D.lists={}))[D.up.name]=z;return{glyphs:1,nodes:1}}
  case'list_get':{const z=D.lists[a.name];let s='';for(const c of z.subarray(a.off,a.off+a.len))s+=String.fromCharCode(c);return{size:z.length,off:a.off,data:btoa(s)}}
  case'list_rm':delete D.lists[a.name];return{};
  case'font_charset':{const a2=[...D.libchars];return{total:a2.length,off:a.off,chars:a2.slice(a.off,a.off+4).join('')}}
  case'font_put_begin':{D.fput={name:a.name,size:a.size,got:0,buf:[],files:(D.fput&&D.fput.files)||{},staged:(D.fput&&D.fput.staged)||{},commits:(D.fput&&D.fput.commits)||0,aborts:(D.fput&&D.fput.aborts)||0,abortAll:false,badOnce:(D.fput&&D.fput.badOnce),failEndFor:(D.fput&&D.fput.failEndFor)};return{chunk:2048}}
  case'font_put':{const f=D.fput;const raw=Uint8Array.from(atob(a.data),c=>c.charCodeAt(0));
   if(f.badOnce){f.badOnce=false;throw Object.assign(new Error('chunk CRC mismatch: send it again'),{detail:'chunk_crc'})}
   if(GlyphPack.crc32(raw)!==a.crc)throw Object.assign(new Error('chunk CRC mismatch: send it again'),{detail:'chunk_crc'});if(a.off<f.got)return{got:f.got};if(a.off!==f.got)throw new Error('out of order');f.buf.push(raw);f.got+=raw.length;return{got:f.got}}
  case'font_put_stat':return{got:D.fput.got};
  case'font_put_abort':D.fput.aborts++;if(a.all){D.fput.staged={};D.fput.abortAll=true}return{};
  case'font_put_end':{const f=D.fput,all=new Uint8Array(f.got);let o=0;for(const p of f.buf){all.set(p,o);o+=p.length}
   const h=[...new Uint8Array(await crypto.subtle.digest('SHA-256',all))].map(x=>x.toString(16).padStart(2,'0')).join('');if(h!==a.sha256)throw new Error('SHA-256 mismatch: send it again');f.staged[f.name]=all;if(f.failEndFor===f.name)throw Object.assign(new Error('SHA-256 mismatch'),{detail:'sha'});return{staged:true,size:all.length,sha256:h}}
  case'font_put_commit':{const f=D.fput;for(const n of a.names)if(!f.staged[n])throw Object.assign(new Error('nothing staged'),{detail:'nothing_staged'});for(const n of a.names){f.files[n]=f.staged[n];delete f.staged[n]}f.commits++;return{committed:a.names.length}}
  case'font_miss':return{total:3,more:0,list:[0x7db2,0x1f600]};
  default:return{}}};
 if(location.search.includes('mockserial')){   /* the real connection code against a pretend port: JSON lines in, "CTL" lines out */
  let push;const enc=new TextEncoder();let buf='';
  const mkWritable=()=>new WritableStream({write(chunk){buf+=new TextDecoder().decode(chunk);let i;
   while((i=buf.indexOf('\n'))>=0){const line=buf.slice(0,i);buf=buf.slice(i+1);if(!line.startsWith('{'))continue;const q=JSON.parse(line);
    push.enqueue(enc.encode('I (123) log line before\n'));
    answer(q.op,q).then(r=>push.enqueue(enc.encode((glued?(glued=false,'psram_free=15264344 l\n'):'')+'CTL '+JSON.stringify({id:q.id,ok:true,...r})+'\n')),e=>push.enqueue(enc.encode('CTL '+JSON.stringify({id:q.id,ok:false,error:e.message,code:e.detail})+'\n')))}}});
  let glued=true;   /* the first reply comes glued behind a stale log fragment (the board's USB ring after a closed period), as does one fragment right after open */
  const fake={open:async o=>{if(!o||o.baudRate!=115200)throw new Error('bad open');fake.opens=(fake.opens||0)+1;fake.isOpen=true;fake.readable=new ReadableStream({start(c){push=c}});fake.writable=mkWritable();buf='';setTimeout(()=>push.enqueue(enc.encode('MEM periodic internal_free=279103 psram_free=1498')),50)},close:async()=>{fake.isOpen=false}};
  Object.defineProperty(navigator,'serial',{value:{requestPort:async f=>{if(!f||!f.filters)throw new Error('no filter');return fake}}});
  if(location.search.includes('early')){   /* a request issued the moment the port is open (inside the quiet window) is answered, not dropped */
   (async()=>{const c=connect();for(let i=0;i<100&&!Conn.io;i++)await new Promise(r=>setTimeout(r,5));
    try{await dev('status',{},3000);document.title='MOCK-EARLY ANSWERED'}catch(e){document.title='MOCK-EARLY FAIL '+e.message}
    await c})()}
  else
  connect().then(async()=>{document.title='MOCK-CONNECTED '+$('#nowt').textContent;
   if(location.search.includes('fx')){const r=[],wait=async f=>{for(let i=0;i<100&&!f();i++)await new Promise(x=>setTimeout(x,20))};
    try{$('nav button[data-t="fx"]').click();await wait(()=>fxLoaded);
     r.push(D.exported&&$('#fxtext').value.startsWith('format = 1')?'EXPORT-OK':'EXPORT-FAIL');
     const cards=[...document.querySelectorAll('#fxnodes .fx')],dimc=cards.find(c=>c.classList.contains('dimrow'));
     r.push(cards.length==D.apps.length&&cards.every((c,i)=>c.querySelector('h3').textContent==D.apps[i].zh)&&dimc&&dimc.querySelector('.why').textContent==='電池供電時暫停'&&!document.querySelector('#fxnodes .fx [value="*new"]')&&$('#fxsel').options[0].textContent=='從檔案匯入…'?'APPS-OK':'APPS-FAIL');
     r.push(cards[0].querySelector('h3').textContent=='音量增益'&&cards[0].querySelectorAll('.sl').length==1&&!/濃度/.test(cards[0].textContent)&&/濃度/.test(cards.find(c=>c.querySelector('h3').textContent=='音色').textContent)?'NOMIX-OK':'NOMIX-FAIL');
     $('#fxsel').value='warm.toml';$('#fxuse').click();await wait(()=>D.selected&&fxApps.find(a=>a.id=='tone').on);
     r.push(D.selected=='warm.toml'&&fxApps.find(a=>a.id=='tone').on&&document.querySelectorAll('#fxnodes .fx input[type=checkbox]:checked').length==3?'IMPORT-OK':'IMPORT-FAIL')}catch(e){r.push('ERR '+e.message)}
    document.title='MOCK-FX '+r.join(' ');return}
   if(location.search.includes('tz')){const r=[],sleep=ms=>new Promise(x=>setTimeout(x,ms)),N=Date.UTC(2026,10,15),Z=z=>tzFromZone(z,N).posix;
    try{const want={'Asia/Taipei':'<+08>-8','UTC':'<+00>0','Asia/Kolkata':'<+0530>-5:30','Asia/Kathmandu':'<+0545>-5:45','America/Sao_Paulo':'<-03>3',
      'America/New_York':'<-05>5<-04>,M3.2.0,M11.1.0','Europe/London':'<+00>0<+01>,M3.5.0/1,M10.5.0','Europe/Berlin':'<+01>-1<+02>,M3.5.0,M10.5.0/3',
      'Australia/Sydney':'<+10>-10<+11>,M10.1.0,M4.1.0/3','Pacific/Auckland':'<+12>-12<+13>,M9.5.0,M4.1.0/3','Australia/Lord_Howe':'<+1030>-10:30<+11>-11,M10.1.0,M4.1.0','America/Santiago':'<-04>4<-03>,M9.1.6/24,M4.1.6/24','America/Nuuk':'<-02>2<-01>,M3.5.0/-1,M10.5.0/0'};
     const bad=Object.entries(want).filter(([z,p])=>{try{return Z(z)!=p}catch(e){return true}}).map(([z])=>z+'='+(()=>{try{return Z(z)}catch(e){return e.message}})());
     r.push(bad.length?'ZONES-FAIL '+bad.join(';'):'ZONES-OK');
     const sets=()=>D.clockSets||[],geo=[];Object.defineProperty(navigator,'geolocation',{value:{getCurrentPosition:(ok)=>{geo.push(1);ok({coords:{latitude:25.0330,longitude:121.5654}})}}});
     await sleep(200);$('#tz-browser').click();await sleep(300);const t=sets().find(x=>'tz'in x);
     r.push(t&&/^<[-+0-9]+>[-0-9:]+(<[-+0-9]+>[-0-9:]*,M\d+\.\d\.\d(\/[\d:]+)?,M\d+\.\d\.\d(\/[\d:]+)?)?$/.test(t.tz)?'BROWSER-OK':'BROWSER-FAIL '+JSON.stringify(t));
     r.push($('#sun-guide').hidden&&!geo.length?'GUIDE-HIDDEN-OK':'GUIDE-HIDDEN-FAIL');
     {const sel=$('#tz-fixed');sel.value='330';sel.dispatchEvent(new Event('change'));await sleep(300);const f=sets().filter(x=>'tz'in x).pop();r.push(f&&f.tz=='<+0530>-5:30'&&sel.options.length==39?'FIXED-OK':'FIXED-FAIL '+JSON.stringify(f)+sel.options.length)}
     $('#sun-set').click();r.push(!$('#sun-guide').hidden&&!geo.length&&/允許/.test($('#sun-guide').textContent)&&/0\.01/.test($('#sun-guide').textContent)?'GUIDE-OK':'GUIDE-FAIL');
     $('#sun-go').click();await sleep(400);const s1=sets().find(x=>'sun_lat'in x);
     r.push(s1&&s1.sun_lat==2503&&s1.sun_lon==12157&&geo.length==1&&!$('#sun-clear').hidden&&/北緯 25.03/.test($('#sun-cur').textContent)?'SUN-SET-OK':'SUN-SET-FAIL '+JSON.stringify(s1)+$('#sun-cur').textContent);
     $('#sun-clear').click();await sleep(400);r.push(sets().some(x=>x.sun_clear)&&$('#sun-clear').hidden&&/尚未設定/.test($('#sun-cur').textContent)?'SUN-CLEAR-OK':'SUN-CLEAR-FAIL')}catch(e){r.push('ERR '+e.message)}
    document.title='MOCK-TZ '+r.join(' ');return}
   if(location.search.includes('settings')){const r=[],sleep=ms=>new Promise(x=>setTimeout(x,ms));      /* the moved settings: a control each, with the range the device had, and a change reaches config_set */
    try{await sleep(200);document.querySelector('nav button[data-t=dev]').click();await sleep(500);
     const moved=['volume_max','rg','rg_untag','cover_smooth','cover_auto','haptic_mv','haptic_ms','home_hold_ms','cast_q','album_sec','album_fade','vocab_new','gapless','dac_bits','ldac_kbps','cstep','font_src','splash'];
     const sels=[...document.querySelectorAll('#dev .set select')],lab=m=>SET[m].label,has=m=>sels.some(x=>x.parentElement.firstChild.firstChild.textContent==lab(m));
     const miss=moved.filter(m=>!has(m));r.push(miss.length?'CTRL-FAIL '+miss.join(','):'CTRL-OK');
     const ctl=m=>sels.find(x=>x.parentElement.firstChild.firstChild.textContent==lab(m));
     const vm=ctl('volume_max'),ru=ctl('rg_untag');
     r.push(vm.options.length==17&&[...vm.options].some(o=>o.value=='85')&&!vm.querySelector('option[value="15"]')&&ru.options.length==19&&[...ru.options].some(o=>o.value=='-12')&&[...ru.options].some(o=>o.value=='6')?'RANGE-OK':'RANGE-FAIL '+vm.options.length+'/'+ru.options.length);
     r.push(!ctl('haptic_mv').querySelector('option[value="0"]')?'HAPTIC-NOOFF-OK':'HAPTIC-NOOFF-FAIL');
     const ds=ctl('dac_bits');ds.value='16';ds.dispatchEvent(new Event('change'));await sleep(200);
     const as=ctl('album_sec');as.value='60';as.dispatchEvent(new Event('change'));await sleep(200);
     const c=D.setCalls||[];r.push(c.some(x=>x.dac_bits===16)&&c.some(x=>x.album_sec===60)?'SET-OK':'SET-FAIL '+JSON.stringify(c));
     r.push(document.querySelector('#g-adv').children.length==7&&document.querySelector('#g-tools').children.length==1?'GROUPS-OK':'GROUPS-FAIL '+document.querySelector('#g-adv').children.length+'/'+document.querySelector('#g-tools').children.length)}catch(e){r.push('ERR '+e.message)}
    document.title='MOCK-SETTINGS '+r.join(' ');return}
   if(location.search.includes('buttons')){const r=[];document.querySelector('#rlist .st button').click();await new Promise(x=>setTimeout(x,300));
    r.push(playingIdx==0?'PLAY-OK':'PLAY-FAIL');$('#editbtn').click();r.push($('#rlist').querySelectorAll('input').length>=2?'EDIT-OK':'EDIT-FAIL');
    document.title='MOCK-BUTTONS '+r.join(' ');return}
   if(location.search.includes('radio')){const r=[],sleep=ms=>new Promise(x=>setTimeout(x,ms)),calls=[],ev=(e,t)=>{e.value=t;e.dispatchEvent(new Event('change'))};
    try{window.fetch=async u=>{u=String(u);calls.push(u);
       if(/all\.api|de1\.api/.test(u))throw new Error('down');                        /* the first two servers are down */
       if(/\/json\/servers/.test(u))return{ok:true,json:async()=>[{name:'nl1.api.radio-browser.info'},{name:'at1.api.radio-browser.info'},{name:'evil.example.com'}]};
       if(/countries/.test(u))return{ok:true,json:async()=>[['US','The United States',900],['DE','Germany',700],['CN','China',300],['JP','Japan',250],['TW','Taiwan',100],['HK','Hong Kong',50]].map(([c,n,k])=>({iso_3166_1:c,name:n,stationcount:k}))};
       return{ok:true,json:async()=>[]}};
     const first=()=>[...document.querySelectorAll('#fpanel .row.wrap button')].map(b=>b.textContent.replace(/\s*\d+$/,''));
     $('nav button[data-t="find"]').click();await sleep(300);showView('country');await sleep(400);
     r.push(rbLast=='de2.api.radio-browser.info'&&localStorage.getItem('rb_last')==rbLast&&calls.some(u=>/de2\.api.*countries/.test(u))?'ROTATE-OK':'ROTATE-FAIL '+rbLast);
     r.push(rbHosts.includes('nl1.api.radio-browser.info')&&!rbHosts.some(h=>/evil/.test(h))&&rbHosts[0]=='all.api.radio-browser.info'?'SERVERS-OK':'SERVERS-FAIL '+rbHosts);
     const tw=first().slice(0,3).join();r.push(tw=='台灣,香港,美國'?'ORDER-TW-OK':'ORDER-TW-FAIL '+tw);
     ev($('#fpanel select'),'1');const cn=first().slice(0,4).join();r.push(cn=='中国,香港,台湾,美国'?'ORDER-CN-OK':'ORDER-CN-FAIL '+cn);
     ev($('#fpanel select'),'2');const en=first().slice(0,6).join();r.push(en=='United States,Germany,China,Japan,Taiwan,Hong Kong'?'ORDER-EN-OK':'ORDER-EN-FAIL '+en);
     r.push(!/[\u{1F1E6}-\u{1F1FF}]|國家/u.test($('#find').textContent)?'NOFLAG-OK':'NOFLAG-FAIL');
     ev($('#rbsel'),'nl1.api.radio-browser.info');calls.length=0;showView('country');await sleep(400);
     r.push(rbPick=='nl1.api.radio-browser.info'&&calls.filter(u=>/countries/.test(u)).every(u=>/nl1\./.test(u))?'PICK-OK':'PICK-FAIL '+calls);
     ev($('#rbsel'),'');
     /* the stations page: the lists, the backup addresses */
     $('nav button[data-t="radio"]').click();await loadStations();
     r.push($('#rlsel').options.length==2&&!$('#rlrow').hidden?'LISTS-OK':'LISTS-FAIL');
     ev($('#rlsel'),'cn');await sleep(300);r.push(D.list=='cn'&&/還沒有電台/.test($('#rlist').textContent)?'SWITCH-OK':'SWITCH-FAIL');
     ev($('#rlsel'),'radio');await sleep(300);$('#editbtn').click();await sleep(50);
     const inp=$('#rlist').querySelectorAll('input')[2];inp.value='http://b1.example/s http://b2.example/s http://b3.example/s';inp.dispatchEvent(new Event('input'));await saveStations();
     r.push(D.putList=='radio'&&JSON.stringify(D.stations[0].alts)=='["http://b1.example/s","http://b2.example/s"]'?'ALTS-OK':'ALTS-FAIL '+JSON.stringify(D.stations[0]))}
    catch(e){r.push('ERR '+e.message)}
    document.title='MOCK-RADIO '+r.join(' ');return}
   if(location.search.includes('lists')){const r=[];try{$('nav button[data-t="lists"]').click();$('#lkind').value='lunch';await listNew('lunch',true);await listUpload();
     r.push(D.lists['lunch.zip']?'UPLOAD-OK':'UPLOAD-FAIL');lm=null;await listFetch({name:'lunch.zip',size:D.lists['lunch.zip'].length});r.push(lm&&lm.groups.length==6?'RELOAD-OK':'RELOAD-FAIL');
     await listPreview();r.push(lres.stats.glyphs>50?'RENDER-OK':'RENDER-FAIL')}catch(e){r.push('ERR '+e.message)}document.title='MOCK-LISTS '+r.join(' ');return}
   if(location.search.includes('langs')){const r=[],sleep=ms=>new Promise(x=>setTimeout(x,ms)),han=/[\u3400-\u9fff]/;   /* the page follows the device, the viewer's menu wins, and no tab keeps text of another language */
    try{await sleep(300);r.push(Lang.cur==0?'FOLLOW-DEV-OK':'FOLLOW-DEV-FAIL '+Lang.cur);
     D.cfg.lang=2;await status();r.push(Lang.cur==2&&$('#conn').textContent=='Disconnect'&&$('#conn-pill').textContent=='Connected'?'DEV-EN-OK':'DEV-EN-FAIL '+Lang.cur+$('#conn').textContent);
     const sel=$('#langsel');sel.value='1';sel.dispatchEvent(new Event('change'));await sleep(200);
     r.push(Lang.cur==1&&document.documentElement.lang=='zh-Hans'&&$('nav button[data-t=radio]').textContent=='电台'&&localStorage.getItem('lang')=='cn'?'PICK-OK':'PICK-FAIL '+Lang.cur);
     D.cfg.lang=0;await status();r.push(Lang.cur==1?'PICK-WINS-OK':'PICK-WINS-FAIL');
     const bad=[];
     for(const L of [2,0,1]){Lang.set(L);await sleep(150);
      for(const t of ['radio','find','fx','lists','dev','flash','fonts']){document.querySelector('nav button[data-t='+t+']').click();await sleep(400);
       if(t=='dev'){$('#sun-set').click();await sleep(50)}
       const scope=[document.querySelector('#top'),document.querySelector('nav'),document.querySelector('section.on')],txt=scope.map(e=>{const c=e.cloneNode(true);c.querySelectorAll('select,option,textarea,pre,#fres,#fxnodes').forEach(x=>x.remove());return c.textContent.replace('語言 / Language','').replace('繁體中文, 简体中文, English','').replace('(繁中 / 简中 / English)','')+' '+[...c.querySelectorAll('[placeholder],[title]')].map(x=>(x.placeholder||'')+(x.title||'')).join(' ')}).join(' ');
       if(L==2&&han.test(txt))bad.push('en/'+t+': '+(txt.match(/.{0,12}[\u3400-\u9fff]+.{0,6}/)||[''])[0]);
       if(L==1&&/[們這裡個機電連線單設]/.test(txt))bad.push('cn/'+t+': '+(txt.match(/.{0,12}[們這裡個機電連線單設].{0,6}/)||[''])[0]);
       if(L==0&&/[们这个机电连线单设]/.test(txt))bad.push('tw/'+t+': '+(txt.match(/.{0,12}[们这个机电连线单设].{0,6}/)||[''])[0])}}
     r.push(bad.length?'TEXT-FAIL '+bad.join(' | '):'TEXT-OK');
     const before=window.__csp.length;let ev='';try{new Function('return 1')()}catch(e){ev=e.name}
     const sc=document.createElement('script');sc.textContent='window.__injected=1';document.head.append(sc);await sleep(50);
     r.push(ev=='EvalError'&&!window.__injected&&window.__csp.length==before+2&&window.__csp.slice(0,before).length==0?'CSP-OK':'CSP-FAIL '+ev+window.__injected+JSON.stringify(window.__csp))}catch(e){r.push('ERR '+e.message)}document.title='MOCK-LANGS '+r.join(' ');return}
   if(new URLSearchParams(location.search).has('fonts')){const r=[],sleep=ms=>new Promise(x=>setTimeout(x,ms)),waitFor=async(f,n=300)=>{for(let i=0;i<n&&!f();i++)await sleep(30);return f()};
    try{await sleep(300);D.libchars='台灣歌選單a音樂';$('nav button[data-t=fonts]').click();
     await waitFor(()=>/\S/.test($('#fo-ext-st').textContent)&&!$('#fo-build').disabled);
     r.push(!$('#fo-build').disabled&&!$('#fo-tc-st').textContent.includes('?')?'SRC-OK':'SRC-FAIL');
     $('#fo-read').click();await waitFor(()=>fontTab.state.device.length>0&&fontTab.state.miss.length>0);
     r.push([...fontTab.state.device].sort().join('')==[...D.libchars].sort().join('')&&fontTab.state.miss.join()=='32178,128512'&&/8/.test($('#fo-chars').textContent)?'CHARSET-OK':'CHARSET-FAIL '+fontTab.state.device+$('#fo-chars').textContent);
     $('#fo-extra').value='網电';$('#fo-build').click();let bdone=false;fontTab.whenBuilt().then(()=>bdone=true);await waitFor(()=>bdone,5000);
     const o=fontTab.state.out;
     if(!o)r.push('BUILD-FAIL '+$('#fo-msg').textContent);else{
      const tm=FontSubset.cmap(o.tc),sm=FontSubset.cmap(o.sc),has=(m,c)=>m.has(c.codePointAt(0));
      r.push(has(tm,'台')&&has(tm,'灣')&&has(tm,'音')&&has(tm,'a')&&!has(tm,'网')&&has(sm,'网')&&has(sm,'电')&&!has(sm,'台')&&!has(sm,'a')?'SPLIT-OK':'SPLIT-FAIL '+tm.size+'/'+sm.size);
      r.push(window.__fontOut.ok&&window.__fontOut.missing>1000&&document.querySelectorAll('#fo-dl button').length==3&&$('#fo-pack').classList.contains('ok')&&/ui-tc\.ttf/.test($('#fo-result').textContent)?'SIZES-OK':'SIZES-FAIL '+JSON.stringify(window.__fontOut));
      D.fput={files:{},aborts:0,badOnce:true};$('#fo-send').click();let sdone=false;(async()=>{for(let i=0;i<4000&&!window.__fontSent;i++)await sleep(30);sdone=true})();await waitFor(()=>sdone,4500);
      const fs=D.fput.files,same=(a,b)=>a&&b&&a.length==b.length&&a.every((x,i)=>x==b[i]);
      r.push(window.__fontSent&&same(fs['ui-tc.ttf'],o.tc)&&same(fs['ui-sc.ttf'],o.sc)&&same(fs['ui-ext.ttf'],o.ext)&&D.fput.commits==1&&!Object.keys(D.fput.staged).length&&/重開機/.test($('#fo-sstat').textContent)?'SEND-OK':'SEND-FAIL '+Object.keys(fs)+$('#fo-sstat').textContent);
      /* the second file fails its final check: nothing of the set is live, what was staged is discarded, the message is in the page's language */
      window.__fontSent=false;D.fput={files:{},staged:{},commits:0,aborts:0,failEndFor:'ui-sc.ttf'};$('#fo-send').click();let sdone2=false;(async()=>{for(let i=0;i<4000&&!/SHA-256/.test($('#fo-sstat').textContent);i++)await sleep(30);sdone2=true})();await waitFor(()=>sdone2,4500);
      r.push(!window.__fontSent&&!Object.keys(D.fput.files).length&&!Object.keys(D.fput.staged).length&&D.fput.abortAll&&D.fput.commits==0&&/整個檔案的 SHA-256 對不上/.test($('#fo-sstat').textContent)&&!/mismatch/.test($('#fo-sstat').textContent)?'SEND-SET-FAIL-OK':'SEND-SET-FAIL-BAD '+JSON.stringify(Object.keys(D.fput.files))+$('#fo-sstat').textContent);
      r.push(/💀|😀|U\+/.test('')?'':'MISSING-LISTED-'+(/[\u3400-\u9fff]/.test($('#fo-missing').textContent)?'OK':'FAIL'))}
    }catch(e){r.push('ERR '+e.message)}document.title='MOCK-FONTS '+r.join(' ');return}
   if(new URLSearchParams(location.search).has('flash')){const r=[],sleep=ms=>new Promise(x=>setTimeout(x,ms));
    window.addEventListener('unhandledrejection',e=>console.log('REJ '+(e.reason&&e.reason.stack||e.reason)));window.addEventListener('error',e=>console.log('ERRV '+e.message));const rp=r.push.bind(r);r.push=x=>{rp(x);console.log('FLASHSTEP '+x)};   /* a stuck run still shows how far it got */
    const E={chip:'ESP32-S31',size:'16MB',pt:null,written:[],after:'',backupRead:0,opened:[],confirms:[],md5bad:false,backupBad:false,denyBackup:false,denyMismatch:false,regs:[],fake};
    const waitFor=async(f,n=100)=>{for(let i=0;i<n&&!f();i++)await sleep(30);return f()},click=async(sel)=>{$(sel).click();await sleep(30)};
    window.confirm=t=>{E.confirms.push(t);return !(E.denyBackup&&/備份/.test(t))&&!(E.denyMismatch&&/警告/.test(t))};
    window.__esptoolMock={Transport:class{constructor(p){E.opened.push(p===fake&&!fake.isOpen&&Conn.state=='flashing')}async disconnect(){}},
     ESPLoader:class{constructor(o){this.chip={CHIP_NAME:E.chip,readMac:async()=>'aa:bb:cc:dd:ee:ff'};E.regs=[]}async writeReg(a,v){if(E.wdtFail)throw new Error('no wdt');E.regs.push([a,v>>>0])}async main(){return E.chip}async detectFlashSize(){return E.size}
      async flashMd5sum(a,n){return E.backupBad?'0'.repeat(32):Flasher.md5Hex(new Uint8Array(n))}
      async readFlash(a,n,cb){if(a==0x8000)return E.pt;E.backupRead=n;if(cb)cb(null,n/2,n);if(cb)cb(null,n,n);return new Uint8Array(n)}
      async writeFlash(o){if(typeof o.calculateMD5Hash!='function')throw new Error('no md5 check requested');for(const f of o.fileArray){const calc=o.calculateMD5Hash(f.data),flash=E.md5bad?'0'.repeat(32):calc;if(calc!=flash)throw new Error('MD5 of file does not match data in flash!')}E.written=o.fileArray.map(f=>({addr:f.address,len:f.data.length,mode:o.flashMode,freq:o.flashFreq,blank:f.data.every(b=>b==255)}));const t=o.fileArray.reduce((a,f)=>a+f.data.length,0);o.fileArray.forEach((f,i)=>{o.reportProgress(i,f.data.length/2,f.data.length);o.reportProgress(i,f.data.length,f.data.length)})}
      async after(m){E.after=m}}};
    const getb=async n=>new Uint8Array(await (await fetch('testdata/merged/'+n)).arrayBuffer());
    const pickB=async n=>{$('#fl-board').value=n;$('#fl-board').dispatchEvent(new Event('change'));await sleep(150)};
    const pickF=async(bytes,name)=>{await flashTab.takeBytes(name,bytes);await sleep(50)};
    const setb=(id,v)=>{$(id).checked=v;$(id).dispatchEvent(new Event('change'))};
    try{await sleep(300);
     const NR='jukedial-0.0.0-test-round.bin',NF='jukedial-0.0.0-test-fakepod.bin',round=await getb(NR),fpi=await getb(NF);E.pt=round.slice(0x8000,0x8000+0xc00);
     $('nav button[data-t=flash]').click();await sleep(100);
     r.push($('#fl-board').options.length==3&&!$('#fl-releases').hidden&&/\/JukeDial\/releases$/.test($('#fl-releases').href)?'LOAD-OK':'LOAD-FAIL');
     /* no board is picked for the viewer, even though the connected player said it is the round one: the menu stays empty, the player's answer is only shown; the download-mode hint waits for a pick */
     r.push($('#fl-board').value==''&&!$('#fl-auto').hidden&&/圓形/.test($('#fl-auto').textContent)&&flashTab.deviceBoard=='jc3636f118'&&/先選板子/.test($('#fl-dl').textContent)&&$('#fl-check').disabled?'NODEFAULT-OK':'NODEFAULT-FAIL '+$('#fl-board').value);
     {await pickB('jc3636f118');const t1=$('#fl-dl').textContent,m1=$('#fl-mismatch').hidden;await pickB('fakepod');const t2=$('#fl-dl').textContent,m2=$('#fl-mismatch').hidden;await pickB('');
      r.push(/針孔/.test(t1)&&/翻面/.test(t1)&&!/兩個按鍵/.test(t1)&&/兩個按鍵/.test(t2)&&/拔掉/.test(t2)&&m1&&!m2?'DLHINT-OK':'DLHINT-FAIL '+JSON.stringify([t1,t2,m1,m2]))}
     {const art=async n=>{await pickB(n);await sleep(200);const v=document.querySelector('#fl-art-img svg');return{svg:!!v,hidden:$('#fl-art').hidden}};
      const a1=await art('jc3636f118'),a2=await art('fakepod'),a3=await art('');r.push(a1.svg&&!a1.hidden&&a2.svg&&a3.hidden?'ART-OK':'ART-FAIL '+JSON.stringify([a1,a2,a3]))}
     /* a picked file: the content is parsed (version, build date, board, SHA-256 of the whole file); with no board picked the check button stays off */
     await pickF(round,NR);
     r.push(flashTab.image&&flashTab.image.board=='jc3636f118'&&/0\.0\.0-test/.test($('#fl-finfo').textContent)&&/[0-9a-f]{64}/.test($('#fl-finfo').textContent)&&$('#fl-check').disabled?'FILE-OK':'FILE-FAIL '+$('#fl-finfo').textContent);
     /* the picked board must be the board the content says: no flash with the other one */
     await pickB('fakepod');r.push(!flashTab.image&&/不會寫入/.test($('#fl-finfo').textContent)&&$('#fl-check').disabled?'BOARD-PICK-REFUSED-OK':'BOARD-PICK-REFUSED-FAIL '+$('#fl-finfo').textContent);
     /* a name that says the other board is not enough, and not needed: the content disagrees with the name */
     await pickB('jc3636f118');await pickF(round,NF);r.push(!flashTab.image&&/檔名和內容不一致/.test($('#fl-finfo').textContent)?'NAME-VS-CONTENT-OK':'NAME-VS-CONTENT-FAIL '+$('#fl-finfo').textContent);
     await pickF(round,'whatever.bin');r.push(flashTab.image&&flashTab.image.board=='jc3636f118'?'NAME-NOT-NEEDED-OK':'NAME-NOT-NEEDED-FAIL');
     /* damage: one flipped byte in the app is a hash error, no flash; a stripped marker is "unknown board" */
     {const bad=round.slice();bad[0x10000+5000]^=1;await pickF(bad,NR);r.push(!flashTab.image&&/SHA-256 不對/.test($('#fl-finfo').textContent)&&$('#fl-check').disabled?'HASH-REFUSED-OK':'HASH-REFUSED-FAIL '+$('#fl-finfo').textContent)}
     await pickF(round,NR);
     /* happy path: the four regions at their offsets, the settings partitions NOT written, backup on, the board and file named in the confirmation */
     await click('#fl-check');await waitFor(()=>!$('#fl-info').hidden);
     r.push(!$('#fl-info').hidden&&Conn.state=='flashing'&&!$('#flashbanner').hidden&&$('nav button[data-t=radio]').disabled&&E.opened[0]&&/ESP32-S31/.test($('#fl-rows').textContent)&&$('#fl-checks .ok')&&/nvs/.test($('#fl-plan').textContent)&&/保留/.test($('#fl-plan').textContent)&&$('#fl-ptnote').hidden?'CHECK-OK':'CHECK-FAIL '+$('#fl-plan').textContent);
     setb('#fl-backup',false);r.push($('#fl-go').disabled&&!$('#fl-skiprow').hidden?'SKIP-GATE-OK':'SKIP-GATE-FAIL');
     setb('#fl-skip',true);setb('#fl-backup',true);E.confirms=[];
     await click('#fl-go');await waitFor(()=>window.__flashDone,200);
     r.push(window.__flashDone&&E.written.map(w=>w.addr).join()=='8192,32768,65536,12582912'&&E.written[0].mode=='keep'&&E.written[0].freq=='keep'&&E.written[3].len==round.length-0xc00000&&E.after=='hard_reset'&&window.__lastBackup&&window.__lastBackup.size==16*1048576&&/^backup_esp32s31_eeff_\d{8}\.bin$/.test(window.__lastBackup.name)&&/圓形/.test(E.confirms[0])&&/0\.0\.0-test/.test(E.confirms[0])&&/保留/.test(E.confirms[0])?'WRITE-OK':'WRITE-FAIL '+JSON.stringify([E.written,E.after,E.confirms[0]]));
     await waitFor(()=>Conn.state!='flashing');
     r.push(Conn.up&&$('#flashbanner').hidden&&!$('nav button[data-t=radio]').disabled&&!$('#fl-done').hidden&&$('#fl-done-fp').hidden?'RESUMED-OK':'RESUMED-FAIL '+Conn.state);
     /* factory reset: the blank data partitions are written too (nothing else changes) */
     window.__flashDone=false;$('#fl-done').hidden=true;E.written=[];await click('#fl-check');await waitFor(()=>!$('#fl-info').hidden);
     setb('#fl-factory',true);setb('#fl-backup',false);setb('#fl-skip',true);await click('#fl-go');await waitFor(()=>window.__flashDone,200);
     r.push(E.written.length==6&&E.written.slice(0,4).map(w=>w.addr).join()=='8192,32768,65536,12582912'&&E.written[4].addr==0x9000&&E.written[4].len==0x6000&&E.written[4].blank&&E.written[5].addr==0xf000&&E.written[5].blank?'FACTORY-OK':'FACTORY-FAIL '+JSON.stringify(E.written));
     await waitFor(()=>Conn.state!='flashing');
     /* the wrong chip is refused: no options, nothing written */
     window.__flashDone=false;$('#fl-done').hidden=true;E.written=[];E.chip='ESP32-C3';await click('#fl-check');await waitFor(()=>!$('#fl-info').hidden);
     r.push($('#fl-opts').hidden&&$('#fl-checks .bad')&&/ESP32-C3/.test($('#fl-checks').textContent)&&!E.written.length?'CHIP-REFUSED-OK':'CHIP-REFUSED-FAIL');
     await click('#fl-cancel');await waitFor(()=>Conn.up);r.push(Conn.up&&$('#fl-info').hidden?'CANCEL-OK':'CANCEL-FAIL '+Conn.state);
     /* a flash smaller than the file is refused */
     E.chip='ESP32-S31';E.size='8MB';await click('#fl-check');await waitFor(()=>!$('#fl-info').hidden);r.push($('#fl-opts').hidden&&/8MB/.test($('#fl-checks').textContent)?'SMALL-REFUSED-OK':'SMALL-REFUSED-FAIL');await click('#fl-cancel');await waitFor(()=>Conn.up);E.size='16MB';
     /* a different partition table in the player: the settings may not be found, said before writing */
     E.pt=new Uint8Array(0xc00);await click('#fl-check');await waitFor(()=>!$('#fl-info').hidden);r.push(!$('#fl-ptnote').hidden?'PT-MOVED-OK':'PT-MOVED-FAIL');await click('#fl-cancel');await waitFor(()=>Conn.up);E.pt=round.slice(0x8000,0x8000+0xc00);
     /* the player said "round" and the viewer picked the FakePod (with the FakePod file): the red line shows, a loud question comes first, "cancel" writes nothing */
     await pickB('fakepod');await pickF(fpi,NF);E.regs=[];E.after='';
     await click('#fl-check');await waitFor(()=>!$('#fl-info').hidden);setb('#fl-backup',false);setb('#fl-skip',true);E.written=[];window.__flashDone=false;
     E.denyMismatch=true;E.confirms=[];await click('#fl-go');await sleep(200);
     r.push(!$('#fl-mismatch').hidden&&/圓形/.test($('#fl-mismatch-t').textContent)&&/FakePod/.test($('#fl-mismatch-t').textContent)&&E.confirms.length==1&&/警告/.test(E.confirms[0])&&!E.written.length&&!window.__flashDone&&Conn.state=='flashing'?'MISMATCH-CANCEL-OK':'MISMATCH-CANCEL-FAIL '+JSON.stringify(E.confirms));
     E.denyMismatch=false;E.confirms=[];await click('#fl-go');await waitFor(()=>window.__flashDone,200);
     r.push(E.confirms.length==2&&/警告/.test(E.confirms[0])&&/FakePod/.test(E.confirms[1])&&/兩個按鍵/.test(E.confirms[1])&&/位址 0x2000/.test(E.confirms[1])&&E.written.length==4?'MISMATCH-YES-OK':'MISMATCH-YES-FAIL '+JSON.stringify(E.confirms));
     /* the FakePod leaves download mode by the S31 LP watchdog (esptool 5.x register sequence), not by RTS; the page says a browser cannot confirm it */
     r.push(JSON.stringify(E.regs)==JSON.stringify([[0x20801018,0x50d83aa1],[0x20801004,2000],[0x20801000,0xd0000102],[0x20801018,0]])&&E.after==''&&!$('#fl-done-fp').hidden?'FAKEPOD-WDT-OK':'FAKEPOD-WDT-FAIL '+JSON.stringify([E.regs,E.after,$('#fl-done-fp').hidden]));
     await waitFor(()=>Conn.state!='flashing');
     /* the flash is compared with the file after the write: a mismatch is an error, never a success; the player is released and told what to do */
     await pickB('jc3636f118');await pickF(round,NR);window.__flashDone=false;window.__flashFailed=false;$('#fl-done').hidden=true;E.written=[];E.md5bad=true;
     await click('#fl-check');await waitFor(()=>!$('#fl-info').hidden);setb('#fl-backup',false);setb('#fl-skip',true);
     await click('#fl-go');await waitFor(()=>window.__flashFailed,200);await waitFor(()=>Conn.up);
     r.push(!window.__flashDone&&!E.written.length&&Conn.up&&$('#fl-info').hidden&&$('#fl-done').hidden&&/可能開不了機/.test($('#fl-msg').textContent)&&/BOOT/.test($('#fl-msg').textContent)?'VERIFY-FAIL-OK':'VERIFY-FAIL-BAD '+Conn.state+$('#fl-msg').textContent);
     E.md5bad=false;
     /* a backup that differs from the flash stops everything before the first write */
     E.backupBad=true;E.written=[];window.__flashFailed=false;await click('#fl-check');await waitFor(()=>!$('#fl-info').hidden);setb('#fl-backup',true);
     await click('#fl-go');await waitFor(()=>/不一致/.test($('#fl-msg').textContent),200);
     r.push(!E.written.length&&!window.__flashDone&&Conn.state=='flashing'&&/備份和快閃記憶體/.test($('#fl-msg').textContent)?'BACKUP-VERIFY-OK':'BACKUP-VERIFY-FAIL '+$('#fl-msg').textContent);
     E.backupBad=false;
     /* the viewer must say the backup file is really there: "no" writes nothing */
     E.denyBackup=true;E.confirms=[];await click('#fl-go');await waitFor(()=>E.confirms.length>=2,200);await sleep(100);
     r.push(!E.written.length&&!window.__flashDone&&E.confirms.length==2&&/備份/.test(E.confirms[1])&&Conn.state=='flashing'?'BACKUP-CONFIRM-OK':'BACKUP-CONFIRM-FAIL '+E.confirms.length+E.written.length);
     E.denyBackup=false;await click('#fl-cancel');await waitFor(()=>Conn.up);
    }catch(e){r.push('ERR '+e.message)}document.title='MOCK-FLASH '+r.join(' ');return}
   if(location.search.includes('pause')){const r=[];try{const p=await Conn.pause('flash');r.push(p===fake&&!fake.isOpen&&Conn.state=='flashing'?'PAUSED-OK':'PAUSED-FAIL');
     const e=await dev('status').catch(x=>x);r.push(e.code=='busy'?'BUSY-OK':'BUSY-FAIL');await Conn.resume();
     const s=await dev('status');r.push(fake.opens==2&&fake.isOpen&&Conn.up&&s.volume!=null?'RESUME-OK':'RESUME-FAIL')}catch(e){r.push('ERR '+e.message)}document.title='MOCK-PAUSE '+r.join(' ');return}
   if(location.search.includes('disconnect')){$('#conn').click();for(let i=0;i<50&&Conn.state!='idle';i++)await new Promise(r=>setTimeout(r,20));
    document.title=Conn.state!='idle'?'MOCK-STILL-OPEN':'MOCK-DISCONNECTED '+$('#conn-pill').textContent+' '+$('#conn').textContent}})}
 else{Conn.useMock(answer);setConnected(true);refreshAll()}}
/* cjk-allow-end */
