python3 setup.py build_ext --inplace
python3 setup.py develop --user
# HACK: need to find a proper way to put the compiled python extension at the right place
#if ! compgen -G "$(pwd)/visky/pyvisky*.so" > /dev/null ; then
#    ln -s $(pwd)/_skbuild/*/cmake-build/pyvisky.*.so $(pwd)/visky/pyvisky.so
#fi
