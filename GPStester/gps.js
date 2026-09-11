(() => {
"use strict";
const KEY="0d5c767a1bc14313828361a42b5cebfe";
const $=id=>document.getElementById(id);
const log=(m,l="INFO")=>{const s=`[${new Date().toLocaleTimeString()}] [${l}] ${m}\n`;console.log(s.trim());if($("log")){$("log").textContent+=s;$("log").scrollTop=$("log").scrollHeight}};
let watchId=null,last=null,prev=null,mode="normal",customBounds=false,deviceHeading=null,map,marker,accuracyCircle,wasm=null;

async function loadWasm(){
 try{
  log("WASM: gps.wasm 読み込み開始");
  try{wasm=(await WebAssembly.instantiateStreaming(fetch("./gps.wasm"),{})).instance.exports}
  catch(e){log("WASM streaming失敗 → ArrayBuffer方式へ","WARN");wasm=(await WebAssembly.instantiate(await (await fetch("./gps.wasm",{cache:"no-store"})).arrayBuffer(),{})).instance.exports}
  log(`WASM: 読み込み完了 version=${wasm.version()} / exports=${Object.keys(wasm).join(",")}`);
 }catch(e){wasm=null;log(`WASM: 読み込み失敗 ${e.message}`,"ERROR")}
}
function fmt(v,d=6){return Number.isFinite(v)?v.toFixed(d):"N/A"}
function geoDistance(a,b){
 if(wasm?.geo_distance_m)return wasm.geo_distance_m(a.lat,a.lon,b.lat,b.lon);
 const R=6371000,p=Math.PI/180,dlat=(b.lat-a.lat)*p,dlon=(b.lon-a.lon)*p;
 const x=dlat/2,y=dlon/2;
 return 2*R*Math.asin(Math.sqrt(Math.sin(x)**2+Math.cos(a.lat*p)*Math.cos(b.lat*p)*Math.sin(y)**2));
}
function gpsHeading(c){
 if(Number.isFinite(c.heading))return{v:c.heading,s:"GPS"};
 if(Number.isFinite(deviceHeading))return{v:deviceHeading,s:"端末コンパス"};
 return null;
}
function setText(id,v){if($(id))$(id).textContent=v}
function update(p){
 last=p;const c=p.coords,now=p.timestamp,lat=c.latitude,lon=c.longitude;
 if(wasm?.valid_latlon&&!wasm.valid_latlon(lat,lon)){log(`WASM: 不正な座標 lat=${lat} lon=${lon}`,"ERROR");return}
 setText("lat",fmt(lat,6));setText("lon",fmt(lon,6));
 if(Number.isFinite(c.altitude)){setText("alt",`${c.altitude.toFixed(1)} m`);setText("altft",`${(wasm?.m_to_ft?wasm.m_to_ft(c.altitude):c.altitude*3.280839895).toFixed(0)} ft`)}
 else{setText("alt","N/A（端末が高度を返していません）");setText("altft","N/A")}
 let sp=null,src="GPS";
 if(Number.isFinite(c.speed)&&c.speed>=0)sp=c.speed;
 else if(prev){const dt=(now-prev.t)/1000,d=geoDistance(prev,{lat,lon});if(dt>0&&dt<120&&d>=0){sp=wasm?.calc_speed?wasm.calc_speed(d,dt):d/dt;src="位置差分計算"}}
 setText("speed",sp!==null?`${sp.toFixed(2)} m/s (${(wasm?.ms_to_kmh?wasm.ms_to_kmh(sp):sp*3.6).toFixed(1)} km/h)`:"N/A（端末が速度を返していません）");
 setText("source",src);setText("accuracy",Number.isFinite(c.accuracy)?`${c.accuracy.toFixed(1)} m`:"N/A");
 const h=gpsHeading(c);setText("heading",h?`${h.v.toFixed(1)}°`:"N/A");
 setText("time",new Date(now).toLocaleString());$("status").textContent=`GPS受信中 / ${mode}`;
 prev={lat,lon,t:now};
 if(marker)marker.setLatLng([lat,lon]);if(accuracyCircle)accuracyCircle.setLatLng([lat,lon]).setRadius(c.accuracy||0);
 if(map&&$("follow")?.checked&&!customBounds)map.setView([lat,lon],Math.max(map.getZoom(),15),{animate:false});
 log(`GPS: lat=${lat}, lon=${lon}, alt=${c.altitude}, speed=${c.speed}, heading=${c.heading}, acc=${c.accuracy}`);
}
function err(e){const names={1:"PERMISSION_DENIED",2:"POSITION_UNAVAILABLE",3:"TIMEOUT"};$("status").textContent=`GPSエラー: ${names[e.code]||e.code}`;log(`Geolocation error: code=${e.code} ${names[e.code]||"UNKNOWN"} ${e.message}`,"ERROR")}
function start(){
 if(!navigator.geolocation){$("status").textContent="Geolocation非対応";return}
 stop(false);prev=null;const opt={normal:{enableHighAccuracy:true,timeout:10000,maximumAge:0},airplane:{enableHighAccuracy:true,timeout:15000,maximumAge:0},train:{enableHighAccuracy:true,timeout:15000,maximumAge:3000}}[mode];
 log(`GPS開始 mode=${mode} options=${JSON.stringify(opt)}`);watchId=navigator.geolocation.watchPosition(update,err,opt);$("status").textContent="GPS取得開始";
}
function stop(write=true){if(watchId!==null){navigator.geolocation.clearWatch(watchId);watchId=null;if(write)log("GPS停止")}}
async function address(){
 if(!last){setText("address","GPS待ち");log("住所取得: GPSデータなし","WARN");return}
 const{latitude,longitude}=last.coords;setText("address","取得中...");
 const u=`https://api.opencagedata.com/geocode/v1/json?q=${encodeURIComponent(latitude+","+longitude)}&key=${encodeURIComponent(KEY)}&language=ja&countrycode=jp&limit=1`;
 log(`Address request: ${latitude},${longitude}`);
 try{const r=await fetch(u,{cache:"no-store"});log(`Address HTTP status=${r.status}`);const d=await r.json();if(!r.ok)throw Error(`HTTP ${r.status}`);if(d.status&&d.status.code!==200)throw Error(`OpenCage ${d.status.code}: ${d.status.message||"unknown"}`);const x=d.results?.[0];if(!x)throw Error("住所結果なし");setText("address",x.formatted||"住所なし");log("Address updated")}
 catch(e){setText("address","取得失敗");log(`Address lookup failed: ${e.message}`,"ERROR");log("APIキー・利用上限・ドメイン制限・CORS/通信状態を確認してください","WARN")}
}
function applyBounds(){
 const n=+$("north").value,s=+$("south").value,w=+$("west").value,e=+$("east").value;
 if(![n,s,w,e].every(Number.isFinite)||n<=s||e<=w||n>90||s<-90||w<-180||e>180){log("範囲指定が不正です","ERROR");return}
 customBounds=true;$("follow").checked=false;map.fitBounds([[s,w],[n,e]],{padding:[15,15]});log(`Map bounds applied N=${n} S=${s} W=${w} E=${e}`)
}
function clearBounds(){customBounds=false;$("follow").checked=true;log("Map bounds cleared");if(last)map.setView([last.coords.latitude,last.coords.longitude],15)}
function setupOrientation(){
 const handler=e=>{let h=null;if(Number.isFinite(e.webkitCompassHeading))h=e.webkitCompassHeading;else if(Number.isFinite(e.alpha))h=(360-e.alpha)%360;if(Number.isFinite(h)){deviceHeading=(h+360)%360;if(!last||!Number.isFinite(last.coords.heading))setText("heading",`${deviceHeading.toFixed(1)}°`)}};window.addEventListener("deviceorientationabsolute",handler,true);window.addEventListener("deviceorientation",handler,true)
}
function initMap(){
 map=L.map("map",{preferCanvas:true}).setView([34.5,131],8);L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",{maxZoom:19,attribution:"&copy; OpenStreetMap contributors"}).addTo(map);marker=L.marker([34.5,131]).addTo(map);accuracyCircle=L.circle([34.5,131],{radius:0}).addTo(map);setTimeout(()=>map.invalidateSize(),100);window.addEventListener("resize",()=>map.invalidateSize())
}
document.addEventListener("DOMContentLoaded",async()=>{
 try{
  log("DOM初期化開始");
  log("検出ボタン: "+[...document.querySelectorAll("button")].map(b=>`${b.id||"(idなし)"}:${b.textContent.trim()}`).join(" / "));
  initMap();
  setupOrientation();
  await loadWasm();

  const bind=(id,event,fn)=>{
   // 固定IDが無い古いHTMLにも対応
   const aliases={start:"startBtn",addr:"addressBtn",apply:"applyBounds",clear:"resetBounds",locate:"currentView"};
   let el=$(id)||$(aliases[id]);
   if(!el){log(`要素が見つかりません: #${id}`,"ERROR");return}
   el.addEventListener(event,fn);
   log(`イベント登録: #${id} ${event}`);
  };

  document.querySelectorAll(".mode").forEach(b=>{
   b.addEventListener("click",()=>{
    document.querySelectorAll(".mode").forEach(x=>x.classList.remove("active"));
    b.classList.add("active");
    mode=b.dataset.mode||"normal";
    log(`Mode changed: ${mode}`);
    if(watchId!==null)start();
   });
  });

  bind("start","click",start);
  bind("stop","click",()=>stop());
  bind("addr","click",address);
  bind("apply","click",applyBounds);
  bind("clear","click",clearBounds);
  bind("locate","click",()=>{
   if(last)map.setView([last.coords.latitude,last.coords.longitude],16);
   else log("現在地へ移動: GPSデータなし","WARN");
  });
  bind("follow","change",()=>{
   customBounds=false;
   log(`GPS auto follow=${$("follow").checked}`);
  });

  log("アプリ起動・ボタン初期化完了");
  if(location.protocol!=="https:"&&location.hostname!=="localhost")
   log("GitHub PagesではHTTPSで開いてください","WARN");
 }catch(e){
  console.error(e);
  log(`初期化エラー: ${e.message}`,"ERROR");
 }
});
})();