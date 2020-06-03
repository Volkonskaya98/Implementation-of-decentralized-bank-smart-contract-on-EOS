#include <string>
#include <vector>
#include <cmath>
#include <ctime>
#include <eosio/eosio.hpp>
#include <eosio/print.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#define _CRT_SECURE_NO_WARNINGS


using namespace eosio;
using namespace std;


class [[eosio::contract("eosbank")]]  eosbank: public eosio::contract {
    public:
    using contract::contract;
    eosbank(name receiver, name code, datastream<const char*> ds):contract(receiver, code, ds),bank_symbol("MLG", 4){}

    //Создание пользователя
    [[eosio::action]]
    void create(
        name user,
        std::string first_name,
        std::string last_name,
        std::string gender,
        uint64_t age,
        long long passport_id) {
        
            require_auth(user);
            check(((gender == "F")||(gender == "M")), "Введите пол в виде F - женский или M - мужской");
            check(age >= 14, "Невозможно зарегистрировать пользователя младше 14 лет");
            check(age < 100, "Невозможно зарегистрировать пользователя старше 100 лет");
           
            address_index addresses(get_self(), get_first_receiver().value);
            auto iterator = addresses.find(user.value); 
            if (iterator ==addresses.end()){
                addresses.emplace(user,[&](auto& row){ 
                    row.key=user;
                    row.first_name=first_name;
                    row.last_name=last_name;
                    row.gender = gender;
                    row.age = age;
                    row.passport_id = passport_id;
                    row.creditstatus = 0;           
                });
            }
            else {

                check(iterator == addresses.end(), "Пользователь уже существует");

            }
    }

    //Открыть счет, пополнить счет
    [[eosio::on_notify("eosio.token::transfer")]]
    void openAccount(name user, name receiver, asset quantity, string memo) {
        if (user == get_self() || receiver != get_self())
        {
            return;
        }
        address_index addresses(get_self(), get_self().value);
        auto iterator = addresses.find(user.value); 
        check(iterator != addresses.end(), "Вы не создали аккаунт");

        uint64_t creditstatus = iterator -> creditstatus;
        check(creditstatus != 3, "Ваш счет заблокирован");

        check(quantity.amount > 0, "Сумма должна быть больше нуля");
        check(quantity.symbol == bank_symbol, "Убедитесь в том, что переводите валюту MLG");

        wallet_table balances(get_self(), get_self().value);
        auto cal_wall = balances.find(user.value);
  
        if (cal_wall != balances.end()){
            asset money = cal_wall -> funds;
            if (money.amount == 0){
                balances.modify(cal_wall, get_self(), [&](auto &row) {
                    row.funds = quantity;
                }); 
            }
            else {
                balances.modify(cal_wall, get_self(), [&](auto &row) {
                    row.funds += quantity;
                }); 
            }
        }    
        else {
            balances.emplace(get_self(), [&](auto &row) {
                row.account = user;
                row.funds = quantity;
                row.data = now();
            });
        }
   
        if (creditstatus == 2){ 
            uint64_t flag = 1;
            isdelay(user, quantity, flag);
            if (check_cr == 1){
                credit_table credits(get_self(), get_first_receiver().value);
                auto credit = credits.find(user.value);
                credits.erase(credit);
            }
            send_summary(user, "Вы положили деньги на счет. Всвязи с задолжностью по кредиту, сумма долга снята с вашего счета.");
        }
        else {
            send_summary(user, "Вы положили деньги на счет в наш банк");
        }
    }

    //Снять деньги с карты
    [[eosio::action]]
    void withdraw(name user, asset quantity) {
        require_auth(user);

        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value); 
        uint64_t creditstatus = iterator -> creditstatus;
        check(creditstatus != 3, "Ваш счет заблокирован");

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find(user.value);
        check(hodl_it != balances.end(), "Вы не открыли счет");

        asset money = hodl_it -> funds;
        check( money >= quantity, "Недостаточно средств для снятия");

        check( quantity.is_valid(), "Некорректная сумма" );
        check( quantity.amount > 0, "Сумма должна быть больше 0" );
        check( quantity.symbol == bank_symbol, "Несоответсвующая валюта. Убедитесь в том, что переводите валюту MLG" );

        if (money == quantity){
            asset zero = money;
            uint64_t a = 0;
            zero.set_amount(a);
            balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                row.funds = zero;
            });
        } 
        else {
            balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                row.funds -= quantity;
            });
        }

        action{
            permission_level{get_self(), "active"_n},
            "eosio.token"_n,
            "transfer"_n,
            std::make_tuple(get_self(), user, quantity, std::string("Снятие денежных средств."))
        }.send();

        send_summary(user, "Вы успешно сняли деньги со счета");
    }

    //Закрыть счет и аккаунт
    [[eosio::action]]
    void close(name user) {
        require_auth(user);
        
        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value); 
        check(iterator != addresses.end(), "У вас нет аккаунта");   
        uint64_t creditstatus = iterator -> creditstatus;
        check(creditstatus != 3, "Ваш счет заблокирован");

        saving_table savings(get_self(), get_first_receiver().value);
        auto saving = savings.find(user.value);
        check(saving == savings.end(), "У вас есть оформленный вклад. Дождитесь его завершения.");
        check(creditstatus == 0, "У вас есть непогашенный кредит");

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find(user.value);
        if (hodl_it != balances.end()) { 
        
            asset money = hodl_it -> funds;
            balances.modify(hodl_it, get_self(), [&](auto &row) {
                row.funds -= money;
            });
            
            if (money.amount > 0) {
                action{
                    permission_level{get_self(), "active"_n},
                    "eosio.token"_n,
                    "transfer"_n,
                    std::make_tuple(get_self(), user, money, std::string("Закрытие аккаунта."))
                }.send();
            }
            
            balances.erase(hodl_it);
        }
        addresses.erase(iterator);
        send_summary(user, "Вы успешно закрыли счет");
    }
    
    //оформить вклад
     [[eosio::action]]
    void saving(name user, asset quantity, uint64_t months){
        require_auth(user);

        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value); 
        check(iterator != addresses.end(), "Вы не создали аккаунт");

        uint64_t creditstatus = iterator -> creditstatus;
        check(creditstatus != 3, "Ваш счет заблокирован");

        check( quantity.is_valid(), "Сумма некорректна" );
        check( quantity.amount >= 10, "Минимальная сумма вклада составляет 10.0000 MLG" ); 
        check( quantity.symbol == bank_symbol, "Несоответсвующая валюта. Убедитесь в том, что переводите валюту MLG" );

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find( user.value);
        check(hodl_it != balances.end(), "Вы не открыли счет");
        
        asset money = hodl_it -> funds;

        check( money >= quantity, "Недостаточно средств для оформления вклада");
        check( months > 0, "Количество месяцев не может быть нулевым");
        
        saving_table savings(get_self(), get_first_receiver().value);
        auto saving = savings.find(user.value);
        check(saving == savings.end(), "У вас уже есть оформленный вклад");
        
        balances.modify(hodl_it, get_self(), [&](auto &row) {
                row.funds -= quantity;
        });
        
        uint32_t data = now();
        uint32_t end_data = data + unix_months * months;
        uint32_t esum = quantity.amount * per_vk / 100; 
        uint64_t all = quantity.amount + esum * months; 

        asset al = quantity;
        al.set_amount(all);

        savings.emplace(get_self(), [&](auto &row) {
            row.account = user;
            row.sum = quantity;
            row.per = per_vk;
            row.months = months;
            row.all = al;
            row.fdata = data;
            row.end_data = end_data;
        });

        send_summary(user, "Вы успешно оформили вклад");
    }

    //Снять вклад
    [[eosio::action]]
    void paysaving(name user) {
        require_auth(user);

        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value); 
        uint64_t creditstatus = iterator -> creditstatus;
        check(creditstatus != 3, "Ваш счет заблокирован");

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find(user.value);
        
        check(hodl_it != balances.end(), "У вас нет открытого счета");

        saving_table savings(get_self(), get_first_receiver().value);
        auto saving = savings.find(user.value);

        check(saving != savings.end(), "У вас нет открытого вклада");

        uint32_t end_data = saving -> end_data; 
        check(now() > end_data, "Время не пришло"); 

        asset money = saving -> all;

        asset funds = hodl_it -> funds;
        if (funds.amount == 0){
            balances.modify(hodl_it, get_self(), [&](auto &row) {
                row.funds = money;
            }); 
        }
        else {
            balances.modify(hodl_it, get_self(), [&](auto &row) {
                row.funds += money;
            }); 
        }

        savings.erase(saving); 
        if (creditstatus == 2){
            uint64_t flag = 1;
            isdelay(user, money, flag);
            if (check_cr == 1){
                credit_table credits(get_self(), get_first_receiver().value);
                auto credit = credits.find(user.value);
                credits.erase(credit);
            }
            send_summary(user, "Вклад успешно закрыт. В связи с задолжностью по кредиту, сумма долга снята с вашего счета.");
        }
        else {
            send_summary(user, "Вклад успешно закрыт, проценты начислены на ваш баланс");
        }
        
    }

    //Перевод средств
     [[eosio::action]]
    void transfer(name user, name to, asset quantity) {
        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value);  
        check(iterator != addresses.end(), "Вы не создали аккаунт");
        uint64_t creditstatus = iterator -> creditstatus;
        auto iterator2 = addresses.find(to.value);  
        check(iterator2 != addresses.end(), "У пользователя нет аккаунта");
        uint64_t creditstatus2 = iterator2 -> creditstatus;
        check(creditstatus != 3, "Ваш счет заблокирован");
        check(creditstatus2 != 3, "Счет получателя заблокирован");

        require_auth(user);
        check( user != to, "Невозможно осущетсвить перевод самому себе" );
        require_recipient( user );
        require_recipient( to );

        check( quantity.is_valid(), "Некорректная сумма" );
        check( quantity.amount > 0, "Сумма должна быть больше 0" );
        check( quantity.symbol == bank_symbol, "Несоответсвующая валюта. Убедитесь в том, что переводите валюту MLG" );

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find(user.value);
        auto hodl_oo = balances.find(to.value);
        
        check(hodl_it != balances.end(), "У вас не открыт счет");
        check(hodl_oo != balances.end(), "У получателя нет открытого счета");

        asset money = hodl_it -> funds;
        check( money >= quantity, "Недостаточно средств для перевода");

        if (money == quantity){
            asset zero = money;
            uint64_t a = 0;
            zero.set_amount(a);
            balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                row.funds = zero;
            });
        } 
        else {
            balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                row.funds -= quantity;
            });
        }
       
        asset funds = hodl_oo -> funds;
        if (funds.amount == 0){
            balances.modify(hodl_oo, get_self(), [&](auto &row) {
                row.funds = quantity;
            }); 
        }
        else {
            balances.modify(hodl_oo, get_self(), [&](auto &row) {
                row.funds += quantity;
            }); 
        }
       
     
        if (creditstatus2 == 2){
            uint64_t flag = 1;
            isdelay(to, quantity, flag);
            if (check_cr == 1){
                credit_table credits(get_self(), get_first_receiver().value);
                auto credit = credits.find(to.value);
                credits.erase(credit);
            }
            send_summary(to, "На ваш счет поступили средства. В связи с задолжностью по кредиту, сумма долга снята с вашего счета.");
        }
        else {
             send_summary(user, "Перевод успешно завершен");
        }
    }

    //заявка на кридит
     [[eosio::action]]
    void credit(name user, asset quantity, uint64_t months){
        require_auth(user);

        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value); 
        check(iterator != addresses.end(), "Вы не создали аккаунт");
        uint64_t creditstatus = iterator -> creditstatus;
        check(creditstatus != 3, "Ваш счет заблокирован");

        check( quantity.is_valid(), "Некорректная сумма" );
        check( quantity.amount >= 10, "Минимальная сумма кредита составляет 10.0000 MLG" );
        check( quantity.symbol == bank_symbol, "Несоответсвующая валюта. Убедитесь в том, что переводите валюту MLG" );
        check( months > 0, "Количество месяцев не может быть нулевым");
        
        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find( user.value);
        check(hodl_it != balances.end(), "Вы не открыли счёт");
        
        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.find(user.value);
        check(credit == credits.end(), "У вас уже есть оформленный кредит");

        request_table requests(get_self(), get_first_receiver().value);
        auto request = requests.find(user.value);
        check(request == requests.end(), "Вы уже подавали заявку на кредит. Ваша заявка находится в рассмотрении.");
   
        requests.emplace(get_self(), [&](auto &row) {
            row.account = user;
            row.sum = quantity;
            row.per = per_cr;
            row.months = months;
            row.data = now();
        });
       
        send_summary(user, "Вы успешно подали заявку на кредитю ");
    }

    //Пополнить кредит пользователем
      [[eosio::action]]
    void paycredit(name user, asset quantity) {
        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value);
        uint64_t creditstatus = iterator -> creditstatus;
        check(creditstatus != 3, "Вы банкрот");
        
        delay_table delays(get_self(), get_first_receiver().value);
        auto delay = delays.find(user.value);

        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.find(user.value);
        check(credit != credits.end(), "У вас нет непогашенного кредита");

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find(user.value);
        asset money = hodl_it -> funds;
        check(money >= quantity, "Недостаточно средств");


        check( quantity.is_valid(), "Некорректная сумма" );
        check( quantity.amount > 0, "Сумма должна быть больше 0" );
        check( quantity.symbol == bank_symbol, "Несоответсвующая валюта. Убедитесь в том, что переводите валюту MLG" );
        
        asset esum = credit -> esum;
        asset done_sum = credit -> done_sum;
        asset rest = credit -> rest;
        asset all = credit -> all; 

        balances.modify(hodl_it, get_self(), [&]( auto& row ) {
            row.funds -= quantity;
        });
        
        if ((done_sum + quantity) >= all){
     
            credits.modify(credit, get_self(), [&](auto &row) {
                    row.done_sum += quantity;
                    row.rest -= quantity; 
            });
            closecredit(user);
            credits.erase(credit);
        }  
       
        else {
      
            if (creditstatus == 2) {
                
                uint64_t flag = 0;
                isdelay(user, quantity, flag);
                if (check_cr == 1){
                    credit_table credits(get_self(), get_first_receiver().value);
                    auto credit = credits.find(user.value);
                    credits.erase(credit);
                }
            }
            else {
               
                credits.modify(credit, get_self(), [&](auto &row) {
                        row.done_sum += quantity;
                        row.rest -= quantity; 
                });
            }
        }
    }
    

    void isdelay(name user, asset quantity, uint64_t flag){

        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value);

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find(user.value);

        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.find(user.value);

        delay_table delays(get_self(), get_first_receiver().value);
        auto delay = delays.find(user.value);

        asset esum = delay -> esum;
        asset done_sum = delay -> done_sum;
        asset next_sum = delay -> next_sum;   
        asset rest = delay -> rest; 
        asset all = delay -> all;   

        if (flag == 0){ 

            if (done_sum.amount + quantity.amount >= (next_sum.amount - esum.amount)) {
             
                if (done_sum + quantity >= all){
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.done_sum += quantity;
                    });
                    closecredit(user);
                }
                else {
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.done_sum += quantity;
                        row.rest -= quantity;
                        row.all = all;
                    });
                    addresses.modify(iterator, get_self(), [&](auto &row) {
                        row.creditstatus = 1;
                    });
                    delays.erase(delay); 
                }                         
            }
            else {
                uint64_t i = 0; 
                while ((done_sum.amount + quantity.amount) >= (done_sum.amount + esum.amount * i)){
                    i++;
                }
                i--;
                delays.modify(delay, get_self(), [&](auto &row) {
                    row.pr_months -= i;
                     row.done_sum += quantity;
                    row.rest -= quantity; 
                });
                credits.modify(credit, get_self(), [&](auto &row) {
                    row.done_sum += quantity;
                    row.rest -= quantity; 
                });
            
            }
        }
        if (flag == 1){

             asset quant; 
            if (all == next_sum){
                quant = rest;
            }
            else {
                quant = next_sum - esum - done_sum;
            }
           
            if (done_sum.amount + quantity.amount >= (next_sum.amount - esum.amount)) {

                rest = all - (done_sum + quant);

                if (done_sum + quant >= all){
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.done_sum += quant;
                    });
                    closecredit(user);
                }
                else {
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.done_sum += quant;
                        row.rest = rest;
                        row.all = all;
                    });
                    addresses.modify(iterator, get_self(), [&](auto &row) {
                        row.creditstatus = 1;
                    }); 
                    delays.erase(delay);
                }

                balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                    row.funds -= quant;
                });
            }
            else {
                uint64_t i = 0;  
                while ((done_sum.amount + quantity.amount) >= (done_sum.amount + esum.amount * i)){
                    i++;
                }
                i--;
                delays.modify(delay, get_self(), [&](auto &row) {
                    row.pr_months -= i;
                    row.done_sum += quant;
                    row.rest -= quant; 
                });
                credits.modify(credit, get_self(), [&](auto &row) {
                    row.done_sum += quant;
                    row.rest -= quant; 
                });

                balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                    row.funds -= quant;
                });
            }  
        }     
    }

    //одобрение заявок менеджером
    [[eosio::action]]
    void processing(){
        require_auth( get_self() );
        
        manager_table manager(get_self(), get_first_receiver().value);
        auto manag = manager.find(get_self().value); 

        if (manag == manager.end()){
             manager.emplace(get_self(),[&](auto& row){ 
                row.account = get_self();
                row.mdata = now() + unix_months;
            }); 
        }
        uint32_t mdata=manag->mdata;

        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.get_index<"secid"_n>();

        request_table requests(get_self(), get_first_receiver().value);
        auto request = requests.get_index<"secid"_n>(); 

        if (now() >= mdata){
            if ((credit.begin() == credit.end())){
                uint64_t i = 0; 
                while (now() >= (mdata + (unix_months * i))){
                    i++;
                }

                manager.modify(manag, get_self(), [&](auto &row) {
                    row.mdata += unix_months*i;
                }); 
            }
            else {
                while (now() >= mdata){
                    review();
                    mdata = manag -> mdata;
                }
            }
        }
        
        for ( auto itr = request.begin(); itr != request.end(); itr++ ) {
    
            name user = itr -> account;
            contropen(user, 1);
          
        }
        auto itr = request.begin();
        while(itr != request.end()) {
            itr = request.erase(itr);
        }
    }

    [[eosio::action]]
    void review(){
        require_auth(get_self());
        manager_table manager(get_self(), get_first_receiver().value);
        auto manag = manager.find(get_self().value); 

        check(manag != manager.end(), "Необходимо авторизоваться за менеджера");

        uint32_t mdata = manag -> mdata;
        check( now() >= mdata, "Еще рано");

        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.get_index<"secid"_n>();

        auto itr = credit.begin(); 
        auto it = credit.begin(); 
        while(itr != credit.end()){
            name user = itr -> account;
            review0(user);
            it = itr;
            itr++;
            if (check_cr == 1){
                credit.erase(it);
            }
            check_cr = 0;
        }

        manager.modify(manag, get_self(), [&](auto &row) {
            row.mdata += unix_months;
        }); 
    }
    
  
    void contropen(name user, uint64_t flag){
       require_auth( get_self() );
        
        manager_table manager(get_self(), get_first_receiver().value);
        auto manag = manager.find(get_self().value); 

        uint32_t now_data = now();
        uint32_t mdata = manag -> mdata;

        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value);
        
        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find( user.value);
        if (hodl_it == balances.end()){
            flag = 0;
        }
   
        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.find(user.value);
        if (credit != credits.end()) { 
                flag = 0;
        }
     
        request_table requests(get_self(), get_first_receiver().value);
        auto request = requests.find(user.value);
        if (request == requests.end()) {
            flag = 0;
        }
        if (flag == 1){
            
            asset sum = request -> sum;
            uint64_t per = request -> per;
            uint64_t months = request -> months;
            uint64_t allsum = (100 + per_cr) * sum.amount  / 100;
            uint64_t ezsum = allsum / months;
            asset all = sum;
            all.set_amount(allsum);
            asset quant = sum;
            quant.set_amount(ezsum);
            asset zero = sum;
            zero.set_amount(0);

            uint32_t end_data = mdata + unix_months * (months - 1);

            credits.emplace(get_self(), [&](auto &row) {
                row.account = user;
                row.sum = sum;
                row.per = per;
                row.months = months;
                row.esum = quant;
                row.done_sum = zero;
                row.next_sum = quant;
                row.rest = all;
                row.end_data = end_data;
                row.data = now();
                row.next_data = mdata;
                row.all = all;
            });
            
            balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                row.funds += sum;
            });
            addresses.modify(iterator, get_self(), [&](auto &row){ 
                row.creditstatus = 1;
            }); 

        }
        if (flag == 0){
            send_summary(user, "Вам отказано в кредите");
            requests.erase(request);
        }
    }

    void review0(name user) {

        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value); 
        uint64_t creditstatus = iterator -> creditstatus;
        check(creditstatus != 3, "Пользователь заблокирован");

        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.find(user.value);
        check(credit != credits.end(), "Пользователь не имеет текущего кредита");

        credits.modify(credit, get_self(), [&](auto &row) {
            row.next_data += unix_months;
        });

        asset sum = credit -> sum;
        uint64_t per = credit -> per;
        uint64_t months = credit -> months;
        asset esum = credit -> esum;
        asset done_sum = credit -> done_sum;
        asset next_sum = credit -> next_sum; 
        asset rest = credit -> rest;          
        uint32_t end_data = credit -> end_data;
        uint32_t data = credit -> data;
        asset all = credit -> all;

        if (now() >= end_data){

            if (done_sum >= all){
                closecredit(user);
            }
            else {
                senddelay0(user);
            }
        }
        else {
            
            if (done_sum >= next_sum){
          
                if ((next_sum + esum) >= all){
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.next_sum = all;
                    });
                }
            
                else {
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.next_sum += esum;
                    });
                }
            }
            else {   
                senddelay(user);
            }
        }
    }

    void senddelay0(name user){
        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value); 

        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.find(user.value);

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find( user.value);

        asset sum = credit -> sum;
        uint64_t per = credit -> per;
        uint64_t months = credit -> months;
        asset esum = credit -> esum;
        asset done_sum = credit -> done_sum;
        asset next_sum = credit -> next_sum;   
        asset rest = credit -> rest;         
        uint32_t end_data = credit -> end_data;
        uint32_t data = credit -> data;
        asset all = credit -> all;

        rest = all - done_sum;
        asset money = hodl_it -> funds;

        if (money >= rest){
            balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                row.funds -= rest;
            });
            closecredit(user);
        }
        else {

            asset zero = sum;
            uint64_t a = 0;
            zero.set_amount(a);

            if (money.amount == 0){
                money = zero;
            }
            balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                row.funds = zero;
            });
            
            uint64_t pros = esum.amount * per_pena /100;
            asset plata = sum; 
            plata.set_amount(pros);
  
            if (money.amount == 0){
                credits.modify(credit, get_self(), [&](auto &row) {
                    row.rest = all - done_sum + plata ;
                    row.next_sum = all + plata;
                    row.all += plata;
                });
            }
            else {
                done_sum += money;
                credits.modify(credit, get_self(), [&](auto &row) {
                    row.done_sum += money;
                    row.rest = all - done_sum + plata ;
                    row.next_sum = all + plata;
                    row.all += plata;
                });
            }

            delay_table delays(get_self(), get_first_receiver().value);
            auto delay = delays.find(user.value);
            if (delay == delays.end()) {
                delays.emplace(get_self(),[&](auto& row){ 
                    row.account = user;
                    row.sum = sum;
                    row.pr_months = 1;
                    row.months = months;
                    row.per = per;
                    row.esum = esum;
                    row.done_sum = done_sum;
                    row.next_sum = all + plata;
                    row.rest = all - done_sum + plata;
                    row.end_data = end_data;
                    row.data = data;
                    row.all = all + plata;                                      
                });
                addresses.modify(iterator, get_self(), [&](auto &row){ 
                    row.creditstatus = 2;
                });   
            }
            else {
                all = credit -> all;;
                delays.modify(delay, get_self(), [&](auto &row) {
                    row.pr_months += 1;
                    row.done_sum += money ;
                    row.rest = all - done_sum;
                    row.next_sum = all;
                    row.all = all;
                });
            }     
            all = delay -> all;   
            rest = delay -> rest;
            done_sum = delay -> done_sum;
            asset dolg = all - done_sum;
            
            if ((delay -> pr_months) == 3){
                saving_table savings(get_self(), get_first_receiver().value);
                auto saving = savings.find(user.value);
                if (saving != savings.end()){
                    closesaving(user, dolg);
                }
            }           
            if ( (delay -> pr_months) == 4){
                bankrupt_table bankrupts(get_self(), get_first_receiver().value);
                auto bankrupt = bankrupts.find(user.value);
                if (bankrupt == bankrupts.end()) {
                    bankrupts.emplace(get_self(),[&](auto& row){ 
                            row.account = user;
                            row.sum = sum;
                            row.per = per;
                            row.months = months;
                            row.esum = esum;
                            row.done_sum = done_sum;
                            row.data = data;
                            row.all = all;
                    });

                    check_cr = 1;
                    delays.erase(delay);  
                    addresses.modify(iterator, get_self(), [&](auto &row){ 
                        row.creditstatus = 3;
                    }); 
                }
            }
        }
    }


    void senddelay( name user){
    
        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value); 

        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.find(user.value);   

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find( user.value);

        asset sum = credit -> sum;
        uint64_t per = credit -> per;
        uint64_t months = credit -> months;
        asset esum = credit -> esum;
        asset done_sum = credit -> done_sum;
        asset next_sum = credit -> next_sum;   
        asset rest = credit -> rest;     
        uint32_t end_data = credit -> end_data;
        uint32_t data = credit -> data;
        asset all = credit -> all;

        asset money = hodl_it -> funds;
        asset raz = next_sum - done_sum;

        if (money >= raz){
            balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                row.funds -= raz;
            });
            if (esum >= (all - next_sum)) {
                credits.modify(credit, get_self(), [&](auto &row) {
                    row.done_sum += raz;
                    row.rest -= raz;
                    row.next_sum = all;
                });

            }
            else {

                credits.modify(credit, get_self(), [&](auto &row) {
                    row.done_sum += raz;
                    row.rest -= raz;
                    row.next_sum += esum;
                });
            }
        }
        else {
            asset zero = sum;
            uint64_t a = 0;
            zero.set_amount(a);

            if (money.amount ==0){
                money = zero;
            }

            balances.modify(hodl_it, get_self(), [&]( auto& row ) {
                row.funds = zero;
            });

            uint64_t pros = esum.amount * per_pena /100;
            asset plata = sum;
            plata.set_amount(pros);
        
            if (esum >= (all - next_sum)) {
                if (money.amount == 0){
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.rest = all - done_sum  + plata;
                        row.next_sum = all + plata;
                        row.all += plata;
                    });
                }
                else {
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.done_sum += money;
                        row.rest = all - done_sum - money + plata;
                        row.next_sum = all + plata;
                        row.all += plata;
                    });
                }
            }
            else {
                if (money.amount == 0){
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.rest = all - done_sum  + plata;
                        row.next_sum += esum; 
                        row.all += plata; 
                    });
                }
                else {
                    credits.modify(credit, get_self(), [&](auto &row) {
                        row.done_sum += money;
                        row.rest = all - done_sum - money + plata;
                        row.next_sum += esum; 
                        row.all += plata; 
                    });
                }
            }

            done_sum = credit -> done_sum;
            next_sum = credit -> next_sum;
            rest = credit -> rest;
            all = credit -> all;
           
            delay_table delays(get_self(), get_first_receiver().value);
            auto delay = delays.find(user.value);
            if (delay == delays.end()) {
                delays.emplace(get_self(),[&](auto& row){ 
                    row.account = user;
                    row.sum = sum;
                    row.months = months;
                    row.pr_months = 1;
                    row.per = per;
                    row.esum = esum;
                    row.done_sum = done_sum + money;
                    row.next_sum = next_sum;
                    row.rest = rest;
                    row.end_data = end_data;
                    row.data = data;
                    row.all = all;                                      
                });
                addresses.modify(iterator, get_self(), [&](auto &row){ 
                    row.creditstatus = 2;
                }); 
            }
        
            else {
                rest = delay -> rest;
                delays.modify(delay, get_self(), [&](auto &row) {
                    row.pr_months += 1;
                    row.done_sum += money;
                    row.next_sum = next_sum;
                    row.rest -= money - plata;
                    row.all += plata;
                });
            }
             
            all = delay -> all; 
            rest = delay -> rest; 
            done_sum = delay -> done_sum;
            asset dolg = next_sum - done_sum - esum;
            
            if ((delay -> pr_months) == 3){
                saving_table savings(get_self(), get_first_receiver().value);
                auto saving = savings.find(user.value);
                if (saving != savings.end()){
                    closesaving(user, dolg);
                }
            }     

            if ( (delay -> pr_months) == 4){
                bankrupt_table bankrupts(get_self(), get_first_receiver().value);
                auto bankrupt = bankrupts.find(user.value);
                if (bankrupt == bankrupts.end()) {
                    bankrupts.emplace(get_self(),[&](auto& row){ 
                            row.account = user;
                            row.sum = sum;
                            row.per = per;
                            row.months = months;
                            row.esum = esum;
                            row.done_sum = done_sum;
                            row.data = data;
                            row.all = all;
                    });
                    
                    check_cr = 1;
                    delays.erase(delay);  
                    addresses.modify(iterator, get_self(), [&](auto &row){ 
                        row.creditstatus = 3;
                    });  
                }
            }
        }
    }

    //Принудительно закрыть вклад, только банком. Если задолженность 3 месяца
    void closesaving(name user, asset quantity){
        uint64_t check_cr = 0;
        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value);

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find(user.value);

        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.find(user.value);
        
        saving_table savings(get_self(), get_first_receiver().value);
        auto saving = savings.find(user.value);

        delay_table delays(get_self(), get_first_receiver().value);
        auto delay = delays.find(user.value);

        uint32_t data = now();
        uint32_t fdata = saving -> fdata;
        uint32_t end_data = saving -> end_data;
        uint64_t months = saving -> months;
        asset sum = saving -> sum;

        uint64_t mon;
        if ( data >= end_data){
            mon = months;
        }
        else {
            uint64_t mon = (data - fdata) / unix_months;
        }
        
        uint64_t esum = quantity.amount * per_vk / 100;
        uint64_t all = sum.amount + esum * mon; 
        sum.set_amount(all); 
        uint64_t flag = 0;

        if (all >= quantity.amount ){
           
            isdelay(user, quantity, flag);
            
            sum = sum - quantity; 
            uint64_t var = 1; 
            //1 вариант - закрываем вклад и остаток пользователю
            if (var == 1){
                asset funds = hodl_it -> funds;
                if (funds.amount == 0){
                    balances.modify(hodl_it, get_self(), [&](auto &row) {
                        row.funds = sum;
                    }); 
                }
                else {
                    balances.modify(hodl_it, get_self(), [&](auto &row) {
                        row.funds += sum;
                    }); 
                }
                savings.erase(saving);
            }
            //или 2 вариант -  оставляем вклад, но перезаписываем - уменьшаем конечную сумму на то что забрали
            if (var == 2){
                if (sum.amount == 0){
                    savings.modify(saving, get_self(), [&](auto &row) {
                        row.all -= quantity;
                    }); 
                }
                else {
                    savings.erase(saving);
                }
            }
            
        }
   
        else {
            isdelay(user, sum, flag);
            savings.erase(saving);

        }
    }

    //Закрытие кредита с возвратом остатка
    void closecredit(name user) {
        
        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value);

        wallet_table balances(get_self(), get_first_receiver().value);
        auto hodl_it = balances.find(user.value);

        credit_table credits(get_self(), get_first_receiver().value);
        auto credit = credits.find(user.value);

        delay_table delays(get_self(), get_first_receiver().value);
        auto delay = delays.find(user.value);
        
        asset sum = credit -> sum;
        asset done_sum = credit -> done_sum;
        asset all = credit -> all;
       
        if (done_sum > all){
            uint64_t ost = done_sum.amount - all.amount;
            asset ostat = sum;
            ostat.set_amount(ost);
            asset funds = hodl_it -> funds;
            if (funds.amount == 0){
                balances.modify(hodl_it, get_self(), [&](auto &row) {
                    row.funds = ostat;
                }); 
            }
            else {
                balances.modify(hodl_it, get_self(), [&](auto &row) {
                    row.funds += ostat;
                }); 
            }
        }
        if (delay != delays.end()){
            delays.erase(delay);
        }

        check_cr = 1;
        
        addresses.modify(iterator, get_self(), [&]( auto& row ) { 
            row.creditstatus = 0;
        });  
        send_summary(user, "Кредит погашен");
    }


//Для тестирования работы всех методов

    //изменить данные пользователя
    //[[eosio::action]]
    void change(name user, uint64_t flag) {
        require_auth( get_self() );
        address_index addresses(get_self(), get_first_receiver().value);
        auto iterator = addresses.find(user.value); 
        if (iterator != addresses.end()) {
            addresses.modify(iterator, get_self(), [&](auto& row){
                    row.creditstatus = flag;                       
                });
        }
    } 

    //удалить из таблиц
    //[[eosio::action]]
    void erase(name user, uint64_t flag){
        
        require_auth(get_self());
        if (flag == 0){
            require_auth( get_self() );
            manager_table manager(get_self(), get_first_receiver().value);
            auto manag = manager.find(user.value);  
            check(manag != manager.end(),"Запись в таблице отсутствует");
            manager.erase(manag); 
        }
        
        //стираем пользователя
        if (flag == 1){
            address_index addresses(get_self(), get_first_receiver().value); 
            auto iterator = addresses.find(user.value);  
            check(iterator != addresses.end(),"Запись в таблице отсутствует");
            addresses.erase(iterator); 
        }
        
        //стираем кошелек
        if (flag == 2){
            
            wallet_table balances(get_self(), get_first_receiver().value);
            auto hodl_it = balances.find(user.value); 
            check(hodl_it != balances.end(),"Запись в таблице отсутствует");
            balances.erase(hodl_it); 
        }
        //удалить вклад
        if (flag == 3){
           
            saving_table savings(get_self(), get_first_receiver().value);
            auto saving = savings.find(user.value);  
            check(saving != savings.end(),"Запись в таблице отсутствует");
            savings.erase(saving); 
        }
        //удалить кредит
        if (flag == 4){
           
            credit_table credits(get_self(), get_first_receiver().value);
            auto credit = credits.find(user.value);  
            check(credit != credits.end(),"Запись в таблице отсутствует");
            credits.erase(credit); 
        }
        //удалить из просрочки
        if (flag == 5){

            delay_table delays(get_self(), get_first_receiver().value);
            auto delay = delays.find(user.value);  
            check(delay != delays.end(),"Запись в таблице отсутствует");
            delays.erase(delay); 
        }
    
           //удалить из банкрота
        if (flag == 6){

            bankrupt_table bankrupts(get_self(), get_first_receiver().value);
            auto bankrupt = bankrupts.find(user.value);  
            check(bankrupt != bankrupts.end(),"Запись в таблице отсутствует");
            bankrupts.erase(bankrupt); 
        }
        
    }


    //Уведомление пользователя о транзакции
    //[[eosio::action]]
    void notify(name user, std::string msg) {
        require_auth(get_self()); 
        require_recipient(user); 
    }

    private:
    const symbol bank_symbol;
    const uint64_t per_cr = 20;
    const uint64_t per_vk = 10;
    const uint64_t unix_months = 2629743;
    const uint64_t per_pena = 10;
    uint64_t check_cr = 0;

    //текущее время дата
    uint32_t now() { return current_time_point().sec_since_epoch(); }

    //Таблица будет содержать людей
    struct [[eosio::table]] person {
        name key; //уникальное значение в качестве первичного ключа для каждого пользователя
        std::string first_name;
        std::string last_name;
        std::string gender;
        uint64_t age;
        long long passport_id;
        uint64_t creditstatus;
        // 0 - чист, 1 - кредит, 2 - delay, 3 - bankrupt

        uint64_t primary_key() const {
            return key.value;
        }
    };     
    typedef
    eosio::multi_index<"people"_n, person> address_index; 

    //таблица балансов
    struct [[eosio::table]] wallets
    {
        name account;
        asset funds;
        uint32_t data;
        auto primary_key() const { return account.value; }
    };
    using wallet_table = eosio::multi_index<"wallet"_n, wallets>;

    //таблица вкладов
     struct [[eosio::table]] savings
    {
        name account;
        asset sum;
        uint64_t per;
        uint64_t months;
        uint32_t fdata;
        uint32_t end_data;
        asset all;
        auto primary_key() const { return account.value; }
    };

    using saving_table = eosio::multi_index<"saving"_n, savings>;

    //таблица заявок на кредит
    struct [[eosio::table]] requests
    {
        name account;
        asset sum;
        uint64_t per;
        uint64_t months;
        uint32_t data;
        uint64_t primary_key() const { return account.value; } 
        uint64_t by_secondary( ) const { return months; }
    };

    typedef 
    eosio::multi_index<"request"_n, requests, eosio::indexed_by<"secid"_n, eosio::const_mem_fun<requests, uint64_t, &requests::by_secondary>>> request_table;

    //таблица текущих кредитов
    struct [[eosio::table]] credits
    {
        name account;
        asset sum;
        uint64_t per;
        uint64_t months;
        asset esum;
        asset done_sum;
        asset next_sum;
        asset rest; 
        uint32_t end_data;
        uint32_t data;
        uint32_t next_data;
        asset all;
        auto primary_key() const { return account.value; }
        uint64_t by_secondary( ) const { return months; }
    };

    typedef 
    eosio::multi_index<"credit"_n, credits, eosio::indexed_by<"secid"_n, eosio::const_mem_fun<credits, uint64_t, &credits::by_secondary>>> credit_table;

    //таблица просроченных кредитов - кредиты с задолженностью
    struct [[eosio::table]] delays
    {
        name account;
        asset sum;
        uint64_t pr_months; 
        uint64_t per;
        uint64_t months;
        asset esum;
        asset done_sum;
        asset next_sum;
        asset rest;
        uint32_t end_data;
        uint32_t data;
        asset all;
        auto primary_key() const { return account.value; }
    };

    using delay_table = eosio::multi_index<"delay"_n, delays>;

    //таблица банкротов - кредиты с задолженностью от 4 месяцев
    struct [[eosio::table]] bankrupts
    {
        name account;
        asset sum;
        uint64_t per;
        uint64_t months;
        asset esum;
        asset done_sum;
        uint32_t data;
        asset all;
        auto primary_key() const { return account.value; }
    };

    using bankrupt_table = eosio::multi_index<"bankrupt"_n, bankrupts>;

    //Управляющий
      struct [[eosio::table]] manager
    {
        name account;
        uint32_t mdata;
        auto primary_key() const { return account.value; }
    };

    using manager_table = eosio::multi_index<"manager"_n, manager>;


    //отправление уведомлений пользователем
    void send_summary(name user, std::string message){
        action(permission_level{get_self(),"active"_n},
        get_self(), 
        "notify"_n, 
        std::make_tuple(user, name{user}.to_string() + message))
        .send(); 
    }
    
};

