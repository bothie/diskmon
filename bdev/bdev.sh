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
	for name in destroy read write short_read disaster_read mmapro mmaprw munmap
	do
		rettype="&error"
		arglist="%error"
		reterror="§error"
		args="!error"
		cleanup_code=")error"
		
		case "$name" in
			destroy)
				rettype="bool"
				arglist=""
				reterror="false"
				args=""
				cleanup_code="
					bdev_deregister_bdev(bdev);
					free(bdev->name);
					free(bdev);
				"
				;;
			
			read)
				rettype="block_t"
				arglist="block_t first, block_t num, unsigned char * data"
				reterror="-1"
				args="first, num, data"
				cleanup_code=""
				;;
			
			write)
				rettype="block_t"
				arglist="block_t first, block_t num, const unsigned char * data"
				reterror="-1"
				args="first, num, data"
				cleanup_code=""
				;;
			
			short_read)
				rettype="block_t"
				arglist="block_t first, block_t num, unsigned char * data, unsigned char * error_map"
				reterror="-1"
				args="first, num, data, error_map"
				cleanup_code=""
				;;
			
			disaster_read)
				rettype="block_t"
				arglist="block_t first, block_t num, unsigned char * data, unsigned char * error_map, const unsigned char * ignore_map"
				reterror="-1"
				args="first, num, data, error_map, ignore_map"
				cleanup_code=""
				;;
			
			mmapro)
				rettype="unsigned char *"
				arglist="block_t first, block_t num"
				reterror="NULL"
				args="first, num"
				cleanup_code=""
				;;
			
			mmaprw)
				rettype="unsigned char *"
				arglist="block_t first, block_t num"
				reterror="NULL"
				args="first, num"
				cleanup_code=""
				;;
			
			munmap)
				rettype="void"
				arglist="unsigned char * address, block_t num"
				reterror=""
				args="address, num"
				cleanup_code=""
				;;
			
			new)
				rettype=""
				arglist=""
				reterror=""
				args=""
				cleanup_code=""
				echo "Error in script $0: Unknown function named $name" >&2
				exit 2
				;;
			
			*)
				echo "Error in script $0: Unknown function named $name" >&2
				exit 2
		esac
		
		namefunc="${name}_function"
		typename="bdev_$namefunc"
		
		echo -e "\tbdev->$name = NULL;\\" >&6
		
		(
			echo -e  "\t$typename $name;"
		) >&5
		
		(
			if test -n "$arglist"
			then
				echo "typedef $rettype(*$typename)(struct bdev * bdev, $arglist);"
			else
				echo "typedef $rettype(*$typename)(struct bdev * bdev);"
			fi
			echo "$typename bdev_get_$namefunc(struct bdev * bdev);"
			echo "$typename bdev_set_$namefunc(struct bdev * bdev, $typename new_$namefunc);"
			if test -n "$arglist"
			then
				echo "$rettype bdev_$name(struct bdev * bdev, $arglist);"
			else
				echo "$rettype bdev_$name(struct bdev * bdev);"
			fi
			echo
		) >&3
		
		(
			if test -n "$arglist"
			then
				echo     "$rettype bdev_$name(struct bdev * bdev, $arglist) {"
			else
				echo     "$rettype bdev_$name(struct bdev * bdev) {"
			fi
			echo -e  "\tif (unlikely(!bdev->$name)) {"
			echo -e  "\t\terrno = ENOSYS;"
			echo -e  "\t\treturn $reterror;"
			echo -e  "\t}"
			echo -e  "\t"
			echo -en "\t"
			if test "$rettype" != "void"
			then
				echo -en "$rettype retval = "
			fi
			if test -n "$args"
			then
				echo -e  "bdev->$name(bdev, $args);"
			else
				echo -e  "bdev->$name(bdev);"
			fi
			if test -n "$cleanup_code"
			then
				echo -e  "\t"
				echo -e  "\t{"
				echo "$cleanup_code"
				echo -e  "\t}"
			fi
			if test "$rettype" != "void"
			then
				echo -e  "\t"
				echo -e  "\treturn retval;"
			fi
			echo     "}"
			echo
			echo     "$typename bdev_get_$namefunc(struct bdev * bdev) {"
			echo -e  "\treturn bdev->$name;"
			echo     "}"
			echo
			echo     "$typename bdev_set_$namefunc(struct bdev * bdev, $typename new_$namefunc) {"
			echo -e  "\t$typename retval = bdev->$name;"
			echo -e  "\tbdev->$name = new_$namefunc;"
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
