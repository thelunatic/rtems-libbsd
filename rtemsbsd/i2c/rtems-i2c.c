#include <machine/rtems-bsd-kernel-space.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include <sys/module.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/bus.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iic.h>
#include <dev/iicbus/iiconf.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <rtems/bsd/local/iicbus_if.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

typedef struct i2c_msg i2c_msg;

struct i2c_softc {
	device_t dev;
	device_t sc_iicbus;
	char *path;
	int fd;
	int mem_rid;
	int irq_rid;
	struct i2c_rdwr_ioctl_data *ioctl_data;
};

static int
rtems_i2c_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev)) {
		return (ENXIO);
	}
	if (!ofw_bus_is_compatible(dev, "rtems,bsp-i2c")){
		return (ENXIO);
	}

	device_set_desc(dev, "RTEMS libbsd I2C");
	return (BUS_PROBE_SPECIFIC);
}

static int
rtems_i2c_attach(device_t dev)
{
	phandle_t node;
	ssize_t compatlen;
	char *compat;
	char *curstr;
	struct i2c_softc *sc;
	int len;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(sc->dev);

	len = OF_getprop(node, "rtems,i2c-path", &sc->path, sizeof(sc->path));
	if (len == -1){
		device_printf(sc->dev, "Path not found in Device Tree");
		return (ENXIO);
	}
	else{
		if ((sc->sc_iicbus = device_add_child(sc->dev, "iicbus", -1)) == NULL) {
			device_printf(sc->dev, "could not allocate iicbus instance\n");
			return (ENXIO);
		}
		config_intrhook_oneshot((ich_func_t)bus_generic_attach, sc->dev);
	}
	return (0);
}

static int
rtems_i2c_detach(device_t dev)
{
	struct i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);

	if ((error = bus_generic_detach(sc->dev)) != 0) {
		device_printf(sc->dev, "cannot detach child devices\n");
		return (error);
	}

	if (sc->sc_iicbus && (error = device_delete_child(dev, sc->sc_iicbus)) != 0)
        return (error);

	return (0);
}

static int
rtems_i2c_transfer(device_t dev, struct iic_msg *msgs, u_int num)
{
	i2c_msg *messages;
	int err;
	char *addr;
	struct i2c_softc *sc;

	sc = device_get_softc(dev);

	/* Open /dev/iic0 */
	sc->fd = open(sc->path, O_RDWR);
	if (sc->fd < 0) {
		device_printf(sc->dev, "%s\n", strerror(errno));
		return errno;
	}

	/* cast iic_msg to i2c_msg */
	messages = (i2c_msg *) &msgs;
	sc->ioctl_data->msgs = messages;
	sc->ioctl_data->nmsgs = num;

	/* IOCTL call to write */
	err = ioctl(sc->fd, I2C_RDWR, &sc->ioctl_data);
	if (err < 0){
		device_printf(sc->dev, "%s\n", strerror(errno));
		close(sc->fd);
		return errno;
	}

	/* Close the device */
	close(sc->fd);

	return (0);
}

static int
rtems_i2c_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr){}

static device_method_t rtems_i2c_methods[] = {
	DEVMETHOD(device_probe,		rtems_i2c_probe),
	DEVMETHOD(device_attach,	rtems_i2c_attach),
	DEVMETHOD(device_detach,	rtems_i2c_detach),
	DEVMETHOD(iicbus_reset,		rtems_i2c_reset),
	DEVMETHOD(iicbus_transfer,	rtems_i2c_transfer),
	DEVMETHOD_END
};

static driver_t rtems_i2c_driver = {
	"rtems_i2c",
	rtems_i2c_methods,
	sizeof(struct i2c_softc),
};

static devclass_t rtems_i2c_devclass;
DRIVER_MODULE(rtems_i2c, simplebus, rtems_i2c_driver, rtems_i2c_devclass, 0, 0);
DRIVER_MODULE(iicbus, rtems_i2c, iicbus_driver, iicbus_devclass, 0, 0);
DRIVER_MODULE(ofw_iicbus, rtems_i2c, ofw_iicbus_driver, ofw_iicbus_devclass, 0, 0);
