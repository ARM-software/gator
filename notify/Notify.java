/**
 * Copyright (C) Arm Limited 2015-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.ServiceManager;

public class Notify {
    public static void main(String[] args) throws RemoteException {
        for (String service : ServiceManager.listServices()) {
            IBinder b = ServiceManager.checkService(service);
            if (b != null) {
                Parcel p = null;
                try {
                    p = Parcel.obtain();
                    b.transact(IBinder.SYSPROPS_TRANSACTION, p, null, 0);
                } finally {
                    if (p != null) {
                        p.recycle();
                    }
                }
            }
        }
    }
}
