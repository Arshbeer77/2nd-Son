import fs from 'node:fs';
import crypto from 'node:crypto';
import path from 'node:path';
const [source, inventory, output = 'out'] = process.argv.slice(2);
if (!source || !inventory) throw Error('Usage: node tools/match_linked_nids.mjs nids.csv inventory.json [output-directory]');
fs.mkdirSync(output, {recursive:true});
const raw = fs.readFileSync(source);
function csv(text) {
  const rows=[]; let row=[],field='',quoted=false;
  for(let i=0;i<text.length;i++) {
    const c=text[i];
    if(c==='"') { if(quoted && text[i+1]==='"'){field+='"';i++;} else quoted=!quoted; }
    else if(c===','&&!quoted){row.push(field);field='';}
    else if(c==='\n'&&!quoted){row.push(field.replace(/\r$/,''));rows.push(row);row=[];field='';}
    else field+=c;
  }
  if(quoted) throw Error('Unclosed CSV quote');
  if(field||row.length){row.push(field.replace(/\r$/,''));rows.push(row);}
  return rows;
}
function nid(name) {
  return crypto.createHash('sha1').update(Buffer.from(name,'utf8')).update(Buffer.from('518d64a635ded8c1e6b039b1c3e55230','hex')).digest().subarray(0,8).reverse().toString('base64').replace(/=+$/,'').replaceAll('/','-');
}
const rows=csv(raw.toString('utf8').replace(/^\uFEFF/,''));
const headers=rows.shift();
for(const key of ['nid','name','library','derived','sources']) if(!headers.includes(key)) throw Error('Missing '+key);
const prior=JSON.parse(fs.readFileSync(inventory,'utf8'));
const wanted=new Set(prior.imports.map(x=>x.encoded.split('#')[0]));
const lookup=new Map(); let count=0; const malformed=[];
for(const row of rows){
  if(row.length===1&&row[0]==='')continue;
  count++;
  if(row.length!==headers.length){
    const relevant=[...wanted].filter(n=>row.join(',').includes(n));
    malformed.push({source_record:count+1,column_count:row.length,relevant_nids:relevant,raw_fields:row});
    if(relevant.length)throw Error('Malformed record contains requested NID: '+relevant.join(','));
    continue;
  }
  const obj=Object.fromEntries(headers.map((h,i)=>[h,row[i]]));
  if(wanted.has(obj.nid)){
    obj.source_record=count+1;obj.name_hash_matches=nid(obj.name)===obj.nid;
    if(!lookup.has(obj.nid))lookup.set(obj.nid,[]);
    lookup.get(obj.nid).push(obj);
  }
}
const results=prior.imports.map(x=>{
  const candidates=lookup.get(x.encoded.split('#')[0])||[];
  const validNames=[...new Set(candidates.filter(c=>c.name_hash_matches).map(c=>c.name))];
  const previous=[...new Set([...x.bloodborne,...x.gow3])];
  return {encoded:x.encoded,previous_names:previous,previously_unmatched:!previous.length,status:validNames.length===1?'unique_hash_verified_name':validNames.length>1?'ambiguous_hash_verified_names':candidates.length?'csv_name_hash_mismatch':'unmatched',names:validNames,candidates,previous_name_conflict:previous.length>0&&validNames.length>0&&!previous.some(p=>validNames.includes(p))};
});
const summary={source,source_sha256:crypto.createHash('sha256').update(raw).digest('hex'),csv_records:count,total_imports:results.length,previously_unmatched:results.filter(x=>x.previously_unmatched).length,new_unique_matches:results.filter(x=>x.previously_unmatched&&x.status==='unique_hash_verified_name').length,total_unique_matches:results.filter(x=>x.status==='unique_hash_verified_name').length,ambiguous:results.filter(x=>x.status==='ambiguous_hash_verified_names').length,unmatched:results.filter(x=>x.status==='unmatched').map(x=>x.encoded),hash_mismatches:results.filter(x=>x.status==='csv_name_hash_mismatch').map(x=>x.encoded),prior_conflicts:results.filter(x=>x.previous_name_conflict).map(x=>x.encoded)};
summary.malformed_records_skipped=malformed.length;
fs.writeFileSync(path.join(output, 'linked-nid-resolution.json'),JSON.stringify({summary,limitations:['Name hashes are verified, but signatures, library versions and runtime implementations are not.','All linked-image imports are covered, including supplied libc and Fios libraries.','CSV source labels and derived flags are retained as supplied, not independently authenticated.'],imports:results},null,2));
fs.writeFileSync(path.join(output, 'linked-nid-source-format-issues.json'),JSON.stringify(malformed,null,2));
const fresh=results.filter(x=>x.previously_unmatched&&x.status==='unique_hash_verified_name');
const lines=['# Second Son NID resolution','',`Source: user-supplied nids.csv; SHA-256 ${summary.source_sha256}.`,'',`Parsed ${count} CSV records. Of ${summary.previously_unmatched} previously unmatched linked-image imports, ${summary.new_unique_matches} now have one hash-verified candidate name. Total uniquely named imports: ${summary.total_unique_matches}/${results.length}.`,'',`Ambiguous matches: ${summary.ambiguous}. Missing NIDs: ${summary.unmatched.length}. Name-hash mismatches: ${summary.hash_mismatches.length}. Conflicts with earlier names: ${summary.prior_conflicts.length}.`,'','Names were checked by recomputing their PS4 NIDs using the algorithm in the previously inspected preparation script. This establishes name-to-NID consistency, not correct function signatures or working implementations. The source CSV was not changed.','', '## Newly identified names','',...fresh.map(x=>`- ${x.encoded}: ${x.names.join(', ')}`),'','## Remaining unmatched','',...summary.unmatched.map(x=>`- ${x}`)];
lines.push('',`Skipped ${malformed.length} malformed CSV record(s). None contained a requested NID. Source record numbers: ${malformed.map(x=>x.source_record).join(', ')}.`, '', '## Complete mapping', '', ...results.map(x=>`- ${x.encoded}: ${x.names.join(', ') || x.status}`));
fs.writeFileSync(path.join(output, 'linked-nid-resolution.md'),lines.join('\n')+'\n');
console.log(JSON.stringify(summary,null,2));
console.log('New examples:',fresh.slice(0,16).map(x=>x.names[0]).join(', '));

