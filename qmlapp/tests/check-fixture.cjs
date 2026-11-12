const fs = require('node:fs');
const path = require('node:path');

const fixturePath = path.join(__dirname, '..', 'fixtures', 'disk-tree.json');
const root = JSON.parse(fs.readFileSync(fixturePath, 'utf8'));
const issues = [];
function visit(node, nodePath = node.name) {
  const children = node.children || [];
  for (const child of children) visit(child, `${nodePath}/${child.name}`);
  if (node.dir && children.length) {
    const sum = children.reduce((total, child) => total + child.size, 0);
    if (sum !== node.size) issues.push(`${nodePath}: declared=${node.size}, children=${sum}, delta=${node.size - sum}`);
  }
}
visit(root);
if (issues.length) {
  console.error('Fixture directory totals do not match children:');
  for (const issue of issues) console.error(`- ${issue}`);
  process.exit(1);
}
console.log('PASS fixture: every populated directory equals the sum of its children');
