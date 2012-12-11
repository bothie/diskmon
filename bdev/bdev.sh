#! /bin/sh

(
	cat >&5 <<EOF
struct bdev {
	void * private;
	char * name;
	block_t size;
	unsigned block_size;
	
EOF
	echo "#define BDEV_CLEAR_FUNCTION_POINTERS(bdev) do {\\" >&6
	for name in destroy read write short_read
	do
		rettype=""
		arglist=""
		reterror=""
		args=""
		cleanup_code=""
		
		case "$name" in
			destroy)
				rettype="bool"
				reterror="false"
				cleanup_code="
					bdev_deregister_bdev(dev);
					free(dev->name);
					free(dev);
				"
				;;
			
			read)
				rettype="block_t"
				arglist=",block_t first,block_t num,unsigned char * data"
				reterror="-1"
				args=",first,num,data"
				;;
			
			write)
				rettype="block_t"
				arglist=",block_t first,block_t num,const unsigned char * data"
				reterror="-1"
				args=",first,num,data"
				;;
			
			short_read)
				rettype="block_t"
				arglist=",block_t first,block_t num,unsigned char * data,unsigned char * error_map"
				reterror="-1"
				args=",first,num,data,error_map"
				;;
			
			new)
				rettype=""
				arglist=""
				reterror=""
				args=""
				cleanup_code=""
				;;
		esac
		
		namefunc="${name}_function"
		typename="bdev_$namefunc"
		
		echo -e "\tbdev->$name=NULL;\\" >&6
		
		(
			echo -e  "\t$typename $name;"
		) >&5
		
		(
			echo "typedef $rettype(*$typename)(void * _private $arglist);"
			echo "$typename bdev_get_$namefunc(struct bdev * dev);"
			echo "$typename bdev_set_$namefunc(struct bdev * dev,$typename new_$namefunc);"
			echo "$rettype bdev_$name(struct bdev * dev $arglist);"
			echo
		) >&3
		
		(
			echo     "$rettype bdev_$name(struct bdev * dev $arglist) {"
			echo -e  "\tif (unlikely(!dev->$name)) {"
			echo -e  "\t\terrno=ENOSYS;"
			echo -e  "\t\treturn $reterror;"
			echo -e  "\t}"
			echo -e  "\t$rettype retval=dev->$name(dev->private $args);"
			echo -e  "\t{ $cleanup_code }"
			echo -e  "\treturn retval;"
			echo     "}"
			echo
			echo     "$typename bdev_get_$namefunc(struct bdev * dev) {"
			echo -e  "\treturn dev->$name;"
			echo     "}"
			echo
			echo     "$typename bdev_set_$namefunc(struct bdev * dev,$typename new_$namefunc) {"
			echo -e  "\t$typename retval=dev->$name;"
			echo -e  "\tdev->$name=new_$namefunc;"
			echo -e  "\treturn retval;"
			echo     "}"
			echo
		) >&4
	done 
	echo "};" >&5
	echo >&5
	echo -e "} while (0)" >&6
) 3>bdev_autogen.h.1.tmp 4>bdev_autogen.c.1.tmp 5>bdev_autogen.h.2.tmp 6>bdev_autogen.c.2.tmp
cat bdev_autogen.h.1.tmp bdev_autogen.h.2.tmp > bdev_autogen.h
rm bdev_autogen.h.1.tmp bdev_autogen.h.2.tmp
cat bdev_autogen.c.1.tmp bdev_autogen.c.2.tmp > bdev_autogen.c
rm bdev_autogen.c.1.tmp bdev_autogen.c.2.tmp
