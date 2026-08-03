script_folder="/Users/beshoyhanna/Development/rigs-of-rods/build-macos-ogre14-roadmap/conan/generators"
echo "echo Restoring environment" > "$script_folder/deactivate_conanrunenv-release-armv8.sh"
for v in OPENSSL_MODULES
do
   is_defined="true"
   value=$(printenv $v) || is_defined="" || true
   if [ -n "$value" ] || [ -n "$is_defined" ]
   then
       echo export "$v='$value'" >> "$script_folder/deactivate_conanrunenv-release-armv8.sh"
   else
       echo unset $v >> "$script_folder/deactivate_conanrunenv-release-armv8.sh"
   fi
done

export OPENSSL_MODULES="/Users/beshoyhanna/.conan2/p/b/opensb4cefc207c363/p/lib/ossl-modules"