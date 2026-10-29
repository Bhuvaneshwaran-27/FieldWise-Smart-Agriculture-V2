import {db,auth,crops} from './_lib.js';
export default async function handler(req,res){if(req.method!=='POST')return res.status(405).json({error:'Method not allowed'});if(!auth(req,res))return;try{
 const b=req.body||{};const raw=Number(b.soil_raw),crop=Number(b.crop_code);
 if(!Number.isInteger(raw)||raw<0||raw>4095||!Number.isInteger(crop)||crop<0||crop>4)return res.status(400).json({error:'Invalid crop or soil data'});
 const fault=raw<=10||raw>=4085||b.soil_moisture===null;
 const soil=fault?null:Number(b.soil_moisture);
 if(!fault&&(!Number.isFinite(soil)||soil<0||soil>100))return res.status(400).json({error:'Invalid soil moisture'});
 const bool=(x)=>x===true;const pump=bool(b.pump_on);
 if(fault&&pump)return res.status(400).json({error:'Pump must be OFF on sensor fault'});
 const optional=(x)=>x===null||x===undefined||x===''?null:Number(x);
 const temperature=optional(b.temperature),humidity=optional(b.humidity),rssi=optional(b.wifi_rssi);
 if([temperature,humidity,rssi].some(x=>x!==null&&!Number.isFinite(x)))return res.status(400).json({error:'Invalid numeric reading'});
 const row={crop_code:crop,crop_name:crops[crop],soil_raw:raw,soil_moisture:soil,temperature,humidity,motion_detected:bool(b.motion_detected),pump_on:pump,irrigation_required:bool(b.irrigation_required),pump_lockout:bool(b.pump_lockout),website_synchronized:bool(b.website_synchronized),wifi_rssi:rssi};
 const {error}=await db().from('sensor_readings').insert(row);if(error)throw error;return res.status(201).json({ok:true,soil_fault:fault});
 }catch(e){console.error(e);return res.status(500).json({error:'Server error'})}}
