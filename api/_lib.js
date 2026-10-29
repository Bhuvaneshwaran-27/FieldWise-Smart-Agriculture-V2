import { createClient } from '@supabase/supabase-js';
export function db(){if(!process.env.SUPABASE_URL||!process.env.SUPABASE_SERVICE_ROLE_KEY)throw Error('Supabase env missing');return createClient(process.env.SUPABASE_URL,process.env.SUPABASE_SERVICE_ROLE_KEY,{auth:{persistSession:false}})}
export function auth(req,res){const key=req.headers['x-device-key'];if(!process.env.DEVICE_API_KEY||key!==process.env.DEVICE_API_KEY){res.status(401).json({error:'Unauthorized'});return false}return true}
export const crops=['Not selected','Rice','Wheat','Cotton','Tomato'];
