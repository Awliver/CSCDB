"""TPC-C schema for RMDB (dialect: int/float/char(N), no NULL).

NULL sentinels (documented deviation — engine has no NULL):
  o_carrier_id = 0      -> order not yet delivered
  ol_delivery_d = 'PENDING' -> order line not yet delivered
c_data is char(50) (spec wants 300..500); h_data char(24).
"""

# (table, DDL) — creation order respects FK-ish dependencies for load
SCHEMA = [
    ("warehouse",
     "create table warehouse (w_id int, w_name char(10), w_street_1 char(20), "
     "w_street_2 char(20), w_city char(20), w_state char(2), w_zip char(9), "
     "w_tax float, w_ytd float);"),
    ("district",
     "create table district (d_id int, d_w_id int, d_name char(10), d_street_1 char(20), "
     "d_street_2 char(20), d_city char(20), d_state char(2), d_zip char(9), "
     "d_tax float, d_ytd float, d_next_o_id int);"),
    ("customer",
     "create table customer (c_id int, c_d_id int, c_w_id int, c_first char(16), "
     "c_middle char(2), c_last char(16), c_street_1 char(20), c_street_2 char(20), "
     "c_city char(20), c_state char(2), c_zip char(9), c_phone char(16), c_since char(30), "
     "c_credit char(2), c_credit_lim int, c_discount float, c_balance float, "
     "c_ytd_payment float, c_payment_cnt int, c_delivery_cnt int, c_data char(50));"),
    ("history",
     "create table history (h_c_id int, h_c_d_id int, h_c_w_id int, h_d_id int, "
     "h_w_id int, h_date char(19), h_amount float, h_data char(24));"),
    ("new_orders",
     "create table new_orders (no_o_id int, no_d_id int, no_w_id int);"),
    ("orders",
     "create table orders (o_id int, o_d_id int, o_w_id int, o_c_id int, "
     "o_entry_d char(19), o_carrier_id int, o_ol_cnt int, o_all_local int);"),
    ("order_line",
     "create table order_line (ol_o_id int, ol_d_id int, ol_w_id int, ol_number int, "
     "ol_i_id int, ol_supply_w_id int, ol_delivery_d char(30), ol_quantity int, "
     "ol_amount float, ol_dist_info char(24));"),
    ("item",
     "create table item (i_id int, i_im_id int, i_name char(24), i_price float, "
     "i_data char(50));"),
    ("stock",
     "create table stock (s_i_id int, s_w_id int, s_quantity int, s_dist_01 char(24), "
     "s_dist_02 char(24), s_dist_03 char(24), s_dist_04 char(24), s_dist_05 char(24), "
     "s_dist_06 char(24), s_dist_07 char(24), s_dist_08 char(24), s_dist_09 char(24), "
     "s_dist_10 char(24), s_ytd float, s_order_cnt int, s_remote_cnt int, s_data char(50));"),
]

INDEXES = [
    "create index warehouse (w_id);",
    "create index district (d_w_id, d_id);",
    "create index customer (c_w_id, c_d_id, c_id);",
    "create index item (i_id);",
    "create index stock (s_w_id, s_i_id);",
    "create index orders (o_w_id, o_d_id, o_id);",
    "create index new_orders (no_w_id, no_d_id, no_o_id);",
    "create index order_line (ol_w_id, ol_d_id, ol_o_id, ol_number);",
]

# CSV column order per table (must match DDL order — load maps by position)
CSV_HEADER = {
    "warehouse": "w_id,w_name,w_street_1,w_street_2,w_city,w_state,w_zip,w_tax,w_ytd",
    "district": "d_id,d_w_id,d_name,d_street_1,d_street_2,d_city,d_state,d_zip,d_tax,d_ytd,d_next_o_id",
    "customer": ("c_id,c_d_id,c_w_id,c_first,c_middle,c_last,c_street_1,c_street_2,c_city,"
                 "c_state,c_zip,c_phone,c_since,c_credit,c_credit_lim,c_discount,c_balance,"
                 "c_ytd_payment,c_payment_cnt,c_delivery_cnt,c_data"),
    "history": "h_c_id,h_c_d_id,h_c_w_id,h_d_id,h_w_id,h_date,h_amount,h_data",
    "new_orders": "no_o_id,no_d_id,no_w_id",
    "orders": "o_id,o_d_id,o_w_id,o_c_id,o_entry_d,o_carrier_id,o_ol_cnt,o_all_local",
    "order_line": ("ol_o_id,ol_d_id,ol_w_id,ol_number,ol_i_id,ol_supply_w_id,"
                   "ol_delivery_d,ol_quantity,ol_amount,ol_dist_info"),
    "item": "i_id,i_im_id,i_name,i_price,i_data",
    "stock": ("s_i_id,s_w_id,s_quantity,s_dist_01,s_dist_02,s_dist_03,s_dist_04,s_dist_05,"
              "s_dist_06,s_dist_07,s_dist_08,s_dist_09,s_dist_10,s_ytd,s_order_cnt,"
              "s_remote_cnt,s_data"),
}

# load order: referenced tables first, biggest last for progress visibility
LOAD_ORDER = ["warehouse", "district", "item", "customer", "history",
              "orders", "new_orders", "stock", "order_line"]

# NULL sentinels (see module docstring)
CARRIER_NULL = 0
DELIVERY_D_NULL = "PENDING"

# spec cardinalities per warehouse
DISTRICTS_PER_W = 10
CUSTOMERS_PER_D = 3000
ORDERS_PER_D = 3000
FIRST_UNDELIVERED_O_ID = 2101   # orders >= this are undelivered at load (spec 4.3.3.1)
ITEMS = 100000
