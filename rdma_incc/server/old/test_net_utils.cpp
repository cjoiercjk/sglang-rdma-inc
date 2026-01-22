#define ENABLE_IB_UTILS
#include"net_utils.hpp"

#include <iostream>
using namespace std;

int main()
{
    cout << string dev_name = std::get<0>(get_device_by_ip("192.168.1.1")) << endl;
    cout << dev_to_ib_dev("ens10f1") << endl;
    cout << dev_to_ib_dev("abc") << endl; // NULL
    cout << ib_dev_to_dev("mlx5_0") << endl;
    cout << get_pci_by_dev("ens10f1") << endl; 
    cout << get_pci_by_dev("abc") << endl; // NULL
    cout << get_socket_by_pci("0000:e3:00.1") << endl;
    for(auto cpu : get_cpu_list_by_socket(0)) cout << cpu << " ";
    cout << endl;
    for(auto cpu : get_cpu_list_by_socket(1)) cout << cpu << " ";
    cout << endl;
    return 0;
}