import {db,auth,crops} from './_lib.js';
export default async function handler(req,res){try{
 if(req.method==='GET'){if(!auth(req,res))return;const {data,error}=await db().from('device_commands').select('*').eq('id','main').single();if(error)throw error;return res.status(200).json({...data,revision:new Date(data.updated_at).getTime()})}
 if(req.method==='POST'){if(!auth(req,res))return;const code=Number(req.body?.crop_code);if(!Number.isInteger(code)||code<0||code>4)return res.status(400).json({error:'Invalid crop_code'});const {data,error}=await db().from('device_commands').upsert({id:'main',crop_code:code,crop_name:crops[code],updated_at:new Date().toISOString()}).select().single();if(error)throw error;return res.status(200).json({...data,revision:new Date(data.updated_at).getTime()})}
 return res.status(405).json({error:'Method not allowed'});
 }catch(e){console.error(e);return res.status(500).json({error:'Server error'})}}
