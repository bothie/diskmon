#include "bdev.h"

#include "common.h"

#include <bterror.h>
#include <errno.h>
#include <string.h>
#include <vasiar.h>

static void bdev_deregister_bdev(struct bdev * bdev);

#include "bdev_autogen.c"

/*
Lohnt sich das?

#define REGISTRY( \
	type \
	register_functions_name, \
		register_functions_args, \
		register_functions_test, \
		register_functions_success_code, \
		register_functions_fail_code, \
	deregister_functions_name, \
		deregister_functions_args, \
		deregister_functions_test, \
		deregister_functions_success_code, \
		deregister_functions_fail_code, \
	lookup_functions_name, \
		lookup_functions_args, \
		lookup_functions_test, \
		lookup_functions_success_code, \
		lookup_functions_fail_code, \
	var \
)

REGISTRY(
	struct bdev *,
	bdev_register_bdev,
		(struct bdev_driver * bdev_driver,struct bdev * bdev),
		!strcmp(iterator->name,bdev->name),{
			INFOF(
				"bdev_register_bdev: %s, bs=%lu, size=%llu."
				,bdev->name
				,bdev->block_size
				,bdev->size
			);
			ignore(bdev_driver);
		},{
			ERRORF("Trying to register an already registered device (%s).",bdev->name);
		},
	bdev_deregister_bdev,
		(struct bdev * bdev),
		!strcmp(iterator->name,bdev->name),{
			INFOF(
				"bdev_dwregister_bdev: %s."
				,bdev->name
			);
		},{
			ERROR("Trying to deregister a device that doesn't exist");
		},
	bdev_lookup_bdev,
		(char * bdev_name),
		!strcmp(iterator->name,bdev_name),0,0,
	bdevs
);
*/

VASIAR(struct bdev *) bdevs;

struct bdev * bdev_register_bdev(
	struct bdev_driver * bdev_driver,
	char * name,
	block_t size,
	unsigned block_size,
//	bdev_read_function read,
//	bdev_write_function write,
	bdev_destroy_function destroy,
	void * private
) {
	ignore(bdev_driver);
	
	if (!destroy) {
		ERRORF("Trying to register a device (%s) without destroy method.",name);
		free(name);
		return NULL;
	}
	
	for (size_t i=0;i<VASIZE(bdevs);++i) {
		struct bdev * iterator=VAACCESS(bdevs,i);
		if (!strcmp(iterator->name,name)) {
			ERRORF("Trying to register an already registered device (%s).",name);
			free(name);
			return false;
		}
	}
	
	struct bdev * bdev=malloc(sizeof(*bdev));
	if (!bdev) {
		ERRORF("While trying to regiert device %s: malloc: %s.",name,strerror(errno));
		free(name);
		return NULL;
	}
	BDEV_CLEAR_FUNCTION_POINTERS(bdev);
	bdev->private=private;
	bdev->name=name;
	bdev->size=size;
	bdev->block_size=block_size;
	bdev->destroy=destroy;
	
	VANEW(bdevs)=bdev;
	
	INFOF(
		"bdev_register_bdev: %s, bs=%u, size=%llu."
		,bdev->name
		,bdev->block_size
		,(unsigned long long)bdev->size
	);
	
	return bdev;
}

static void bdev_deregister_bdev(struct bdev * bdev) {
	for (size_t i=0;i<VASIZE(bdevs);++i) {
		struct bdev * iterator=VAACCESS(bdevs,i);
		if (bdev==iterator) {
			free(VAACCESS(bdevs,i)->name);
			VAACCESS(bdevs,i)=VAACCESS(bdevs,VASIZE(bdevs)-1);
			VATRUNCATE(bdevs,VASIZE(bdevs)-1);
			return;
		}
	}
	ERROR("Trying to deregister a device that doesn't exist");
}

struct bdev * bdev_lookup_bdev(const char * bdev_name) {
	for (size_t d=0;d<VASIZE(bdevs);++d) {
		struct bdev * bdev=VAACCESS(bdevs,d);
		if (!strcmp(bdev->name,bdev_name)) {
			return bdev;
		}
	}
	errno=ENOENT;
	return NULL;
}

const char * bdev_get_name(struct bdev * bdev) {
	return bdev->name;
}

block_t bdev_get_size(struct bdev * bdev) {
	return bdev->size;
}

unsigned bdev_get_block_size(struct bdev * bdev) {
	return bdev->block_size;
}

#if 0
block_t bdev_read(struct bdev * dev,block_t off,block_t num,unsigned char * buf) {
/*
	printf(
		"%s->read %llu blocks @ %llu\n"
		,dev->name
		,(unsigned long long)num
		,(unsigned long long)off
	);
*/
	if (unlikely(!dev->read)) {
		errno=ENOSYS;
		return -1;
	}
	return dev->read(dev->private,off,num,buf);
}

block_t bdev_write(struct bdev * dev,block_t off,block_t num,const unsigned char * buf) {
	printf(
		"%s->write %llu blocks @ %llu\n"
		,dev->name
		,(unsigned long long)num
		,(unsigned long long)off
	);
	if (unlikely(!dev->write)) {
		errno=ENOSYS;
		return -1;
	}
	return dev->write(dev->private,off,num,buf);
}

void bdev_destroy(struct bdev * dev) {
	printf(
		"%s->destroy\n"
		,dev->name
	);
	dev->destroy(dev->private);
	bdev_deregister_bdev(dev);
	free(dev->name);
	free(dev);
	return;
}
#endif // #if 0

VASIAR(struct bdev_driver *) bdev_drivers;

struct bdev_driver * bdev_register_driver(const char * name,bdev_driver_init_function init) {
	struct bdev_driver * dr;
	for (size_t d=0;d<VASIZE(bdev_drivers);++d) {
		dr=VAACCESS(bdev_drivers,d);
		if (!strcmp(dr->name,name)) {
			ERRORF("Trying to register an already registered driver name %s",name);
			return NULL;
		}
	}
	dr=malloc(sizeof(*dr));
	dr->name=name;
	dr->init=init;
/*
	printf("VASIZE(drivers)=%u\n",VASIZE(drivers));
	printf("&VAACCESS(drivers,0)=%p\n",&VAACCESS(drivers,0));
	printf("drivers.addr.nontype=%p\n",drivers.addr.nontype);
	printf("vasiar_access(...)=%u\n",vasiar_access(
		&drivers.addr.nontype,
		&drivers.num,
		&drivers.max,
		sizeof(*drivers.addr.vartype),
		0
	));
	VAACCESS(drivers,0)=dr;
	printf("Still alive after VAACCESS(drivers,0)=dr\n");
*/
	VANEW(bdev_drivers)=dr;
	return dr;
}

void bdev_deregister_driver(const char * name) {
	for (size_t d=0;d<VASIZE(bdev_drivers);++d) {
		struct bdev_driver * dr=VAACCESS(bdev_drivers,d);
		if (!strcmp(dr->name,name)) {
			free(dr);
			VAACCESS(bdev_drivers,d)=VAACCESS(bdev_drivers,VASIZE(bdev_drivers)-1);
			VATRUNCATE(bdev_drivers,VASIZE(bdev_drivers)-1);
			return;
		}
	}
	ERROR("Trying to deregister a device that doesn't exist");
}

struct bdev_driver * bdev_lookup_driver(const char * name) {
	for (size_t d=0;d<VASIZE(bdev_drivers);++d) {
		struct bdev_driver * dr=VAACCESS(bdev_drivers,d);
		if (!strcmp(dr->name,name)) {
			return dr;
		}
	}
	errno=ENOENT;
	return NULL;
}
