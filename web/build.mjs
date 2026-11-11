import {mkdir,copyFile,cp} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
const root=path.dirname(fileURLToPath(import.meta.url)),dist=path.join(root,'dist');
await mkdir(dist,{recursive:true});
for(const file of ['index.html','styles.css'])await copyFile(path.join(root,file),path.join(dist,file));
await cp(path.join(root,'src'),path.join(dist,'src'),{recursive:true});
console.log(`Static website ready: ${dist}`);
