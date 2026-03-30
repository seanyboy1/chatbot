const { execSync } = require('child_process');
const path = require('path');

exports.default = async function(context) {
  const appPath = context.appOutDir;
  console.log('Stripping extended attributes from:', appPath);
  execSync(`xattr -cr "${appPath}"`, { stdio: 'inherit' });
};
