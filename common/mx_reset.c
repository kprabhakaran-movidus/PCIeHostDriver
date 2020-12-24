/*******************************************************************************
 *
 * MX device reset infrastructure
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 ******************************************************************************/

#include "mx_reset.h"

#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "mx_common.h"
#include "mx_pci.h"
#include "mx_print.h"

/* Time given to MX device to detect and perform reset, in milliseconds.
 * This value may have to be updated if the polling task's period on device side
 * is changed. */
#define MX_DEV_RESET_TIME_MS 1000

/* Myriad Port Logic Vendor Specific DLLP register and configuration value */
#define MX_VENDOR_SPEC_DLLP     (0x704)
#define MX_RESET_DEV            (0xDEADDEAD)

#define PERICOM_SWITCH_DID      (0x2304)

int mx_reset_restore_and_check_device(struct mx_dev *mx_dev) {
    /* Restore the device's context. */
    mx_pci_dev_ctx_restore(mx_dev);

    /* Re-enable the device. */
    mx_pci_dev_enable(mx_dev);

    /* Check that the MX device is back in boot mode.
     * This implies setting MSI enable because the MX is waiting for this to
     * complete part of its initialization process needed to be able to read
     * MMIO space (and thus the operation mode). But the device will not start
     * sending MSIs before other parts of its initialization sequence (implying
     * other handshakes handled by higher layers) are completed. */
    mx_pci_msi_set_enable(mx_dev, 1);
    msleep(1);
    if (mx_get_opmode(mx_dev) != MX_OPMODE_BOOT) {
        return -EIO;
    }

    return 0;
}

//I think current form of this function is to reset one device, not the entire structure
//[aboji]TODO: remove link retraining enable for both devices, add it just for the requested device
//function should be called fore each device which is reset
//to sustain this theory, looking through the devices registered (ls /dev/), each MX has its own file descriptor, so reset needs to be individual.
int mx_reset_device(struct mx_dev *mx_dev) {
    int error;
    int try = 0;
    int port_cnt = 0;
    u16 link_ctl = 0;
    u16 link_stat = 0;
    u32 buses = 0;
    u8 primary, secondary, subordinate;


    //[aboji]TODO: create a dinamically allocated list to remove hardcoding number of devices
    //[aboji]TODO: replace with struct pci_dev** pdev
    struct pci_dev *pdev = NULL;
    struct pci_dev *upstream=NULL;

    /* Save the device's context because its PCIe controller will be reset in
     * the process. */
    mx_pci_dev_ctx_save(mx_dev);

    /* Disable the device to put an end to all operations. */
    mx_pci_dev_disable(mx_dev);

    /* Ensure there are no transactions pending. */
    mx_pci_wait_for_pending_transaction(mx_dev);

    /* Write the magic into Vendor Specific DLLP register to trigger the device
     * reset. */
    pci_write_config_dword(mx_dev->pci, MX_VENDOR_SPEC_DLLP, MX_RESET_DEV);

    /* find the percom device and pass the retrain link command */
    //TODO find specific, mx_dev structure should hold the required primary bus (secondary bus of the upstream port)
    //=> we need to find the primary bus of the upstream port
    // I think we want to remove identifying the upstream port as PERICOM, this should work regardless of the DID.
    //to access upstream device: pdev->bus->parent->self
    pdev = mx_dev->pci;
    upstream = pdev->bus->parent->self;

    while(try++ < 100) {
        int count = 0;
        /* Give some time to the device to trigger and complete the reset. */
        msleep(10);

        //[aboji]TODO: remove the count, just do this for the device in question
        //Multiple devices should be handled at an upper layer.

       pcie_capability_read_word(upstream, PCI_EXP_LNKCTL, &link_ctl);
       link_ctl |= PCI_EXP_LNKCTL_RL;
       pcie_capability_write_word(upstream, PCI_EXP_LNKCTL, link_ctl);

       pcie_capability_read_word(upstream,  PCI_EXP_LNKSTA, &link_stat);
       printk("[bus %02x-%02x] link_ctl %02x link_stat %02x\n", secondary, subordinate, link_ctl, (link_stat&PCI_EXP_LNKSTA_LT));
    }

    /* decrement the reference count and release */
    //[aboji]TODO: just one device
    //pci_dev_put(pdev[0]);
    //pci_dev_put(pdev[1]);

    /* Give some time to the device to trigger and complete the reset. */
    //msleep(MX_DEV_RESET_TIME_MS);

    /* Check that the device is up again before restoring the full PCI context. */
    if (!mx_pci_dev_id_valid(mx_dev)) {
        mx_err("device not valid, returning \n");
        return -EIO;
    }

    /* Restore the full PCI context and check the device is up and running. */
    error = mx_reset_restore_and_check_device(mx_dev);
    if (error) {
        mx_err("mx_reset_restore_and_check_device returned error \n");
        return error;
    }

    return 0;
}
