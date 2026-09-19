#!/usr/bin/env fish

set script_dir (cd (dirname (status --current-filename)); and pwd)

if not type -q npm
    echo "npm não foi encontrado." >&2
    exit 1
end

cd "$script_dir"
npm install
or exit $status
npm run prepare-app
