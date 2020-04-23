/* Copyright (C) 2015-2020 by Arm Limited. All rights reserved. */

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
