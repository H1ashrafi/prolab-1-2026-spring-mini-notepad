/* =============================================================
   Mini Notepad++ - Backend Motoru (Basitlestirilmis Surum)
   Bu kod sadece mantiksal arka plan islemlerini yapar.
   Her satir ogrenci dostu olacak sekilde detayli aciklanmistir.
   ============================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =============================================================
   SABITLER VE YAPILAR (STRUCTS)
   ============================================================= */

/* Geri Alma (Undo) islemlerini belirten sabitler.
   Hangi tur islemin geri alinmasi gerektigini anlamamizi saglar. */
const int ACT_INSERT_CHAR   = 0; /* Karakten ekleme islemi */
const int ACT_DELETE_CHAR   = 1; /* Karakter silme islemi */
const int ACT_ENTER         = 2; /* Enter tuşuna basma (Satir bolme) islemi */
const int ACT_MERGE_LINE    = 3; /* Iki satiri birlestirme islemi (Satir basindayken silme) */
const int ACT_DELETE_WORD   = 4; /* Kelime silme islemi */
const int ACT_REPLACE_ALL   = 5; /* Tumunu degistir islemi */

/* Satirlari hafizada tutmak icin cift yonlu bagli liste (Doubly Linked List) yapisi. */
struct LineNode {
    char *text;               /* Satirin icerdigi metni tutan dinamik dizi (pointer) */
    int len;                  /* Satirin icindeki mevcut karakter sayisi */
    int cap;                  /* Hafizada (text pointer) bu satir icin ayrilmis toplam alan (byte) */
    struct LineNode *prev;    /* Bir onceki satira isaret eden pointer (ilk satirsak NULL) */
    struct LineNode *next;    /* Bir sonraki satira isaret eden pointer (son satirsak NULL) */
};

/* Geri Alma (Undo) yigini (Stack) icin her bir kaydi tutan yapi.
   Bir islem yapilmadan once o anki durum buraya kopyalanir. */
struct UndoAction {
    int type;                 /* Yapilan islemin turu (Yukariya bakin: ACT_INSERT_CHAR vb.) */
    int cur_line;             /* Islem aninda imlecin hangi satirda oldugu (1'den baslar) */
    int cur_col;              /* Islem aninda imlecin hangi sutunda (karakter index) oldugu (0'dan baslar) */
    
    int line_no;              /* Geri yuklenecek asil satirin numarasi */
    char *saved_text;         /* Geri yuklenecek birinci satirin islem oncesi kopyalanmis hali */
    
    int line_no2;             /* Eger islem 2 satiri etkiliyorsa ikinci satirin numarasi */
    char *saved_text2;        /* Ikinci satirin islem oncesi kopyalanmis hali (varsa) */
};

/* Geri Alma (Undo) islemlerini bir liste seklinde tutan Stack (Yigin) yapisi. */
struct UndoStack {
    struct UndoAction items[256]; /* En fazla 256 islemi hafizada tutmak icin sabit boyutlu dizi */
    int top;                      /* Yiginin (Stack) en ustundeki indeksi (-1 olursa yigin bos demektir) */
};

/* =============================================================
   GLOBAL DEGISKENLER
   (Bunlar EditorState yerine kullanilir.)
   ============================================================= */

/* Satir listesinin (Doubly Linked List) basi ve sonu */
struct LineNode *head = NULL; /* Metin belgesinin 1. satiri */
struct LineNode *tail = NULL; /* Metin belgesinin son satiri */
int total_lines = 0;          /* Belgedeki toplam satir sayisi */

/* Imlecin nerede oldugunu tutan degiskenler */
struct LineNode *cur_line_ptr = NULL; /* Imlecin o an icinde bulundugu satiri gosteren pointer */
int cur_line = 1;                     /* Imlecin satir numarasi (Gorsel amacli) */
int cur_col = 0;                      /* Imlecin sutun numarasi (0'dan baslar) */

/* Dosya durumu ile ilgili degiskenler */
int is_dirty = 0;                     /* Belgede degisiklik yapildiysa (Henuz kaydedilmediyse) 1 olur */

/* Undo stack (geri alma gecmisi) degiskeni */
struct UndoStack undo_stack;

/* =============================================================
   TEMEL BAGLI LISTE (LINKED LIST) OPERASYONLARI
   ============================================================= */

/* YENI SATIR OLUSTURMA */
/* Hafizada (RAM) yeni ve bos bir satir (LineNode) icin yer ayirir (allocate) ve baslangic degerlerini ayarlar.
   Eger hafizada yeterli alan yoksa fonksiyon NULL (BOS) dondurur. */
struct LineNode* ln_new() {
    struct LineNode *n = (struct LineNode *) malloc(sizeof(struct LineNode));
    if (n == NULL) return NULL; /* Hafiza ayrilamadiysa islemi durdur/hata dondur */
    
    n->cap = 64; /* Baslangic olarak 64 karakterlik yer (byte) serbest birakiyoruz */
    n->text = (char *) malloc(n->cap);
    if (n->text == NULL) {
        free(n); /* text ayrilamadiysa demin ayirdigimiz "n" yi de sisteme geri verelim (memory leak olmasin) */
        return NULL;
    }
    
    n->text[0] = '\0'; /* Ilk karakterin (0. index) Null Terminator olmasini istiyoruz, yani string BOS */
    n->len = 0;        /* Hic karakter yok */
    n->prev = NULL;    /* Listeye eklendigi yere gore sonra belirlenecek */
    n->next = NULL;
    
    return n;
}

/* SATIRI HAFIZADAN SILME */
/* Programi kapatirken veya bir satir tumuyle silindiginde (ornegin iki satir birlestiginde) ayrilmis (malloc edilmis) alani (free) ismine geri verir. */
void ln_free(struct LineNode *n) {
    if (n != NULL) {
        if (n->text != NULL) {
            free(n->text); /* Once icindeki metni (string) siliyoruz */
        }
        free(n); /* Sonra satir node'unun kendisini siliyoruz */
    }
}

/* SATIR KAPASITESINI BUYUTME */
/* Metin yazdikca satirin icindeki yazi boyutu (len) büyür. Biz bastan 64 karakter (cap) ayarlamistik.
   Kullanici daha cok yazi yazarsa bu 'text arrayini' realloc metodu ile buyutmeliyiz. */
void ln_grow(struct LineNode *n, int needed_len) {
    /* Eger var olan kapasite bittiyse (+1: '\0' karakterini tutmak icin) kapasitesini iki katina cikaralim */
    if (needed_len + 1 > n->cap) {
        n->cap = (needed_len + 1) * 2; /* Iki katina cikart (bu genel bir programlama yaklasimidir, yavaslatmamak icin yapiyoruz) */
        
        /* realloc: Mevcut (n->text) pointer'indaki veriyi alip yeni ve daha genis bir hafiza blokuna kopyalar ve tasiyorsa oraya tasinir */
        char* new_text = (char *) realloc(n->text, n->cap);
        if (new_text != NULL) {
             n->text = new_text; /* realloc islemi eger hafiza yetersizse basarisiz olur. Eger nullptr (basarisiz olmamissa) pointerimiz degistirilir. */
        }
        /* Eger islem basarisiz olmussa (NULL donduyse) bir sey yapamayiz (Gercek senaryoda hata uretilmeli) */
    }
}

/* LISTEDE BELIRLI BIR NUMARADAKI SATIRI BULMA */
/* head (ilk satir) node'undan baslayarak teker teker asagi dogru isaretcilere zipip (next diyerek), 
   istenen "line_num" sayisina ulasiriz. Olasilikla islenen nodeu doner*/
struct LineNode* ln_at(int line_num) {
    if (line_num < 1) return head; /* Hatali param gelebilir. O yüzden baslangıctan donmek islevini korur. */
    
    struct LineNode *c = head;
    int i = 1; /* Ilk satir 1 varsayiyoruz */
    while (c != NULL && i < line_num) {
        c = c->next; /* Bir sonraki bagli listemize dugumumuze  gec */
        i++;
    }
    return c;
}

/* ARAYA YENI LISTE SATIRI EKLEME */
/* referans'dan (ref) hemen sonraya (next) listeye yeni bir LineNode (newn) baglama */
void ln_ins_after(struct LineNode *ref, struct LineNode *newn) {
    if (ref == NULL) {
        /* reference null. Yani liste bom bos! */
        newn->prev = NULL;
        newn->next = NULL;
        head = newn; /* Tek liste head de artik "newn", tail "new" */
        tail = newn;
    } else {
        /* ref bir sey degil */
        newn->prev = ref; /* Yeni "previous" o anki durdugum yer oldu(ref) */
        newn->next = ref->next; /* Guncel durum (ref) nexte benim (newn) next e konuldu(belki de null olacak eger sonda isem)*/
        
        if (ref->next != NULL) {
            ref->next->prev = newn; /* Sonraki arkadasin önceki next'i baglarsaydim artk benim */
        } else {
            /* Demek ki ref tailmis (listenin sonu.) Ben (new) artk benim (newn) tail olacak*/
            tail = newn;
        }
        
        ref->next = newn; /* Suanki "reference" un nextini artk beni o yap.*/
    }
    total_lines++;
}

/* LISTEDEN BIR NODE'U (SATIRI) CIKARMA */
/* Eger iki satir birbirine enter silme (baglanma/birlestirme) sebebi ile birlesiyorsa kullanmali  */
void ln_del_node(struct LineNode *n) {
    /* Onceki dugum var mi diye (en ust liste basi degilmi) */
    if (n->prev != NULL) {
        n->prev->next = n->next; /* Benden "bir sonrasini" benden "bir oncesi"ne ekleyiver.*/
    } else {
        /* Önceki (prev)= NULL, bu bas (head). Artik  beni siliyorlarsa, "bir sonrasi" artk bastir */
        head = n->next;
    }
    
    /* İleriki dugum var mi diye (en alt eleman degil mi diye bakarsan) */
    if (n->next != NULL) {
        n->next->prev = n->prev; /* Benden "bir oncesi" artik benden "bir sonrasinin" pdev' dir.*/
    } else {
        /* Sonraki (next ) = NULL is tail dir. Artik ben tail isem benden bi once tail dir */
        tail = n->prev;
    }
    
    total_lines--;
    ln_free(n); /* Bu satiri bosa atiyoruz */
}


/* SATIRI BOLME (ENTER BASTIGIMIZDA IMLECIN SONRASINI BIR ALT SATIRA ATMA) */
/* "col" pozisyonundan sonrasini al, yeni bir "LineNode" dondur, su anki string'i de "col" olarak kisalt */
struct LineNode* ln_split(struct LineNode *n, int col) {
    struct LineNode *right = ln_new(); /* Asagya eklenecek olan yeni alt satir olusturuldu (ayirma islemi) */
    int tail_len = n->len - col; /* Mevcut satirda enter tusuna basilan karakter indexinden "sonrasini" hesala */
    
    if (tail_len > 0) {
        ln_grow(right, tail_len); /* "right" yani eklenecek satirda yeterli alan ayir.  */
        
        /* Ikinci satira olan verileri kopylama ve asagya alacagiz*/
        int i;
        for (i = 0; i < tail_len; i++) {
           right->text[i] = n->text[col + i];
        }
        
        right->text[tail_len] = '\0'; /* Yeni stringi sonlandirma null'ini koy*/
        right->len = tail_len; /*  uzunlugunu  belirledik */
    }
    
    n->text[col] = '\0';  /* Bizim mevcut su(ustte kalicak) an ki yazi stringimizin null'in (karakter silme islemi yerine) "col" isaret edicez */
    n->len = col; /* Suanki string artk boyle (ust satir uzunlugu) */
    
    return right; /* Alt yariya kopyaladigimiz LineNode u cagiricaktin doder*/
}


/* IKI SATIRI BIRLESTIR (ALT SATIRI UST SATIRA ZIMBALAMA ) */
/* "second" (Ikinci) stringi (Node'nu) "first"'in (Birinci'nin) alt listelerine birlestir. Daha sonra genelde, second i listteden cikaratacagiz.*/
void ln_merge(struct LineNode *first, struct LineNode *second) {
    /* Mesela : birinci (5 karakter) , ikinci(10k) */
    ln_grow(first, first->len + second->len); /*  (5 + 10) byte yer genistetmek icin (first string'inin sonu). */
    
    int i;
    int first_end = first->len; /* Birlestirma nerden basliyacak yeri (index sonu). (null harfini oldugu alan) */
    
    for (i = 0; i < second->len; i++) {
        first->text[first_end + i] = second->text[i]; /* harf haef  second'daki kelimeleri teker teker first(Ilk)'e ekledik .*/
    }
    
    first->len += second->len; /* Length i duzeltmeyi isin en basinda yapamamistik sonrasinda toplayip 15  ekledik .*/
    first->text[first->len] = '\0'; /* Her ne sekilde yeni dizi bitti, son noktasina (NULL(bitti)) karakteri  yapmali */
}

/* =============================================================
   UNDO (GERI ALMA) ISLEMLERI
   ============================================================= */

/* STACKE ISLEM EKLE (UNDO)*/
void undo_push(int type, int ln1, const char *txt1, int ln2, const char *txt2) {
    /* Top, sonu (yigin sinirinda,  maksimum da tutucak kapasite de). */
    if (undo_stack.top >= 255) {
        /* Yigin (eski hatasi sil) doluyisa 1 numarayi kaybeticegiz , ustune ekleyip gidecegiz.. (Stack  O(n) kaydir) */
        if (undo_stack.items[0].saved_text != NULL) { free(undo_stack.items[0].saved_text); }
        if (undo_stack.items[0].saved_text2 != NULL) { free(undo_stack.items[0].saved_text2); }
        
        int i;
        /* "for with shift!" : Butun recordlar  (hafizanin alt satirlarindan "sifir" daki indexten itibaren silinerek  uzerine bir alta ezilir.)*/
        for (i = 0; i < 255; i++) {
            undo_stack.items[i] = undo_stack.items[i + 1];
        }
        undo_stack.top = 254;   /* Sayi dizgesi kuralina 255->254 azaltir.*/
    }
    
    undo_stack.top++; /*  Sonunda index 'i arrttirdik (-1 ilken 0).*/
    int current_index = undo_stack.top;
    
    /* Topa degerleri kaydediyoruz */
    undo_stack.items[current_index].type = type;
    undo_stack.items[current_index].cur_line = cur_line; /* O andaki  islem ne sekilde ise imlecin (Line) bilgileri kopyala .*/
    undo_stack.items[current_index].cur_col = cur_col;   /* O andaki islem  icn  (index , col)  bilgisi */
    undo_stack.items[current_index].line_no = ln1;

    /* `strdup` gibi eger String verilmemise kendimiz manuel allocate (kopyala yapiyoruz string pointer referansi).  */
    if (txt1 != NULL) {
        int length = strlen(txt1);
        undo_stack.items[current_index].saved_text = (char *) malloc(length + 1);
        strcpy(undo_stack.items[current_index].saved_text, txt1);
    } else {
        undo_stack.items[current_index].saved_text = NULL;
    }
    
    undo_stack.items[current_index].line_no2 = ln2;
    
    if (txt2 != NULL) {
        int length = strlen(txt2);
        undo_stack.items[current_index].saved_text2 = (char *) malloc(length + 1);
        strcpy(undo_stack.items[current_index].saved_text2, txt2);
    } else {
        undo_stack.items[current_index].saved_text2 = NULL;
    }
}

/* Sadece "su an imlecin" yazi (Undo hafiza kayitli tutar(Snapshot).) */
void undo_snapshot(int type) {
     undo_push(type, cur_line, cur_line_ptr->text, 0, NULL);
}


/* =============================================================
   EDITOR API KULLANIMLARI (MANTIK(LOGICAL) OPERASYONLARI
   ============================================================= */

/* Duzenleyici ( Editor ) sistemin (bellek/hafiza ) sifirlenip temizligi yapilir. */
void init_editor() {
    /* Stack dizini (undo ) -1 , eklenebilcek halde isret ettik (-1 e listeyi ayarladik)*/
    undo_stack.top = -1;
    
    /* Gelen bagli listeyi (head =null i )ilk bos satira atariz.*/
    struct LineNode *first_line = ln_new();
    head = tail = NULL; 
    total_lines = 0;
    
    ln_ins_after(NULL, first_line); /* Null parametresi ona en uste , "ilke (ilk satira)"  baslamasini soyledi.*/
    
    cur_line = 1;
    cur_col = 0;
    cur_line_ptr = head;
}


/* BIR KARAKTER (String harfi)  Ekleme (Araya / Sona ) . */
/* Notasyon string araya girmesinde memmove ! Yok . */
void insert_char(char ch) {
    /* Yapilan islemi, eski texti yigmaya iter*/
    undo_snapshot(ACT_INSERT_CHAR);
    
    /* Uzunlugun yetmemesi karsiliginda kapasite ayarlar..*/
    ln_grow(cur_line_ptr, cur_line_ptr->len + 1);
    
    int i; 
    /* Stringde yer acmak ,(Karakter 'sag'  uzerina eziyor/ itekleyip yer gosteririz (For loop right to left)) */
    for (i = cur_line_ptr->len; i > cur_col; i--) {
        cur_line_ptr->text[i] = cur_line_ptr->text[i - 1]; 
    }
    
    /* Ozel karakteri simdi belirttigimi yere yazariz .*/
    cur_line_ptr->text[cur_col] = ch;
    cur_line_ptr->len++; /* boyut  +1 , artmis sayildi (gordugumuz listeye/array )*/
    cur_line_ptr->text[cur_line_ptr->len] = '\0'; /* Son NULL kontrol (guvenlik icin).! */
    
    cur_col++; /* Kursorumu z' da saga tasinir. */
    is_dirty = 1; /* Artik bu document bozuldu (edited , un-saved) oldu*/
}


/* KARAKTER (BACKSPACE VEYA NORMAL SILINMESINDE ) */
void delete_char() {
    /* Bazi listeler de 0 a indginde / bir seyler yaziyorsa. (!=0(*/
    if (cur_col > 0) {
        
        /* Satirdan (Ayni satirdan karakter string azaltarak uzerinden silme )*/
        undo_snapshot(ACT_DELETE_CHAR); 
        cur_col--; /* Imlec asagi indexi siliyor ki geriye dogru bi karakter git.*/
        
        int i;
        /* Yazilmis (Sola dogru yazma ) eziyoruz.  Solundan-> sag kayarak o karakter uzerini siliyoruz). */
        for (i = cur_col; i < cur_line_ptr->len - 1; i++) {
           cur_line_ptr->text[i] = cur_line_ptr->text[i + 1]; /* Bir sonraki harfi mevcut sutuna tasiriz.*/
        }
        
        cur_line_ptr->len--; /* Toplam uzunluk artk azaldi . */
        cur_line_ptr->text[cur_line_ptr->len] = '\0'; /* null terminatorden eziyruz kactik !*/

    } else if (cur_line > 1) {
        /* "cur_col" i imlec "0 (sifir)" nci indexde ! Eger yukarda bi list varsa , satirlari , Backspace ustekli liste ve bizi birlestir*/
        
        struct LineNode *prev_node = cur_line_ptr->prev; 
        
        /* Undo kopyalama icin iksi ni Snapshot "ACT_MERGE_LINE" (2 liste arrayni) gonder*/
        undo_push(ACT_MERGE_LINE, cur_line - 1, prev_node->text, cur_line, cur_line_ptr->text);
        
        int prev_old_len = prev_node->len; /* Baglamadan ust satirina gidecegimiz , ust satirin sonundaki , bagli indexinin cursor u ! */
        
        /* Iki satir zimbasi!*/
        ln_merge(prev_node, cur_line_ptr); 
        
        /* Uzerinde islem biten(suan listesi  uzerinde yazildigmiz yer) de silinir. Ptr u degisiririz.. */
        struct LineNode *node_to_delete = cur_line_ptr;
        
        cur_line--; /* Imlec numaramiz bir ust sattiya gectigimiz duser */
        cur_line_ptr = prev_node; /* Arti cursor baglanan yepyeni "eski oncesine " gecer*/
        cur_col = prev_old_len; /* Imleci 'de tam arasinda (birlestigi alan da uzerinde kalmasi) biraktik.. */
        
        ln_del_node(node_to_delete); /* Ve demin altda olan line da bellekden uctu  .. */
    }
    
    is_dirty = 1;
}

/* KELIME SILME  (CTRL + BACKSPACE )  ISLEVI */
void delete_word() {
    /* Silenecek ilk node da ise ve basta ise yok !*/
    if (cur_col == 0 && cur_line == 1) return;
    
    undo_snapshot(ACT_DELETE_WORD); /* Oncesi kopya!*/
    
    if (cur_col == 0) {
         /* Ayni mantik -  cursor de isek ve birlesiyorsa! ( Yukardaki delete_char kodunun aynsidir)  */
         struct LineNode *prev_node = cur_line_ptr->prev;
         int pl = prev_node->len;
         ln_merge(prev_node, cur_line_ptr);
         struct LineNode *node_to_delete = cur_line_ptr;
         
         cur_line--;
         cur_line_ptr = prev_node;
         cur_col = pl;
         ln_del_node(node_to_delete);
    } else {
         /* Ayni satir uzerinden bir KELIME SILYORUZ : kelimenin old. kadar  harfi gezin*/
         int end_col = cur_col;
         int temp_col = cur_col;
         
         /* Geriye dooru bosluklar var sa bunlari zira atliyoruz (Oteki kelimeleri dokuma kadar) */
         while (temp_col > 0 && cur_line_ptr->text[temp_col - 1] == ' ') {
            temp_col--;
         }
         
         /* Kelimemiza dokundysak  karaktrlere gore (Bosluk karakterleri  gelene kadar  tekrardan ) sola!  */
         while (temp_col > 0 && cur_line_ptr->text[temp_col - 1] != ' ') {
             temp_col--;
         }
         
         int deleted_count = end_col - temp_col; /* kac harf karakter islem /yontem ile gectir diyelim .*/
         
         int i;
         /* 'for' dongosu sayesinde bu isaretledim (gezinci / gecilen) kismini atlayerek listeki  (geri silyiryz) sola dogru yazma yapar*/
         for (i = temp_col; i < cur_line_ptr->len - deleted_count; i++) {
             cur_line_ptr->text[i] = cur_line_ptr->text[i + deleted_count];
         }
         
         cur_line_ptr->len -= deleted_count; /* Size azalilmisit. . */
         cur_line_ptr->text[cur_line_ptr->len] = '\0'; /* Sona terminator..  */
         
         cur_col = temp_col; /* Sutun' da artik nerde durduysa ..*/
    }
    
    is_dirty = 1;
}


/* SATIRI ASAGI ENTER YAZMAK ( BOLME / NEW LINE) Islemi !*/
void enter_line() {
     /* Simdiki imlec uzeriden bolunucek text (Snapshot )*/
    undo_snapshot(ACT_ENTER);
    
    /* Simdi String i Cursor de uzerinden ikiyee dogru bolen fonksiyon !! */
    struct LineNode *new_node_for_bottom = ln_split(cur_line_ptr, cur_col);
    
    /* Bunu cift liste baglycisna entegre  edelim! */
    ln_ins_after(cur_line_ptr, new_node_for_bottom);
    
    /* Gostergeler de o zaman asagi (ikinci satra) tasinimaldir: */
    cur_line++; 
    cur_line_ptr = new_node_for_bottom; /* Yep yeni alan.*/
    cur_col = 0; /* Sutun (index) deki harf ilk konumu dondurur. (Sifirncci satirinda ). */
    
    is_dirty = 1;
}


/* =============================================================
   ARAMA (SEARCH) - VE - METIN TUMUYLE DEGISTIRME (REPLACE ALL) 
   ============================================================= */

/* Arama: her eslesmenin satir ve sutununu yazar, sonunda toplam sayiyi yazar.
   Sutun 0'dan baslar, "g satir sutun" komutuyla ayni sekilde. */
void search_text(const char *term) {
    if (term == NULL || strlen(term) == 0) return;
    
    int term_len = strlen(term);
    int current_lno = 1;
    struct LineNode *cur = head;
    int count = 0;
    
    /* LinkedList uzerinde teker gezin , eger harf  denkligini ("strstr" (string karsilastir ) ile esliyoruz))   */
    while (cur != NULL) {
        char *ptr_found = strstr(cur->text, term); /* "cur->text" i tara, term yoksa null 'u cevir !. */
        
        while (ptr_found != NULL) {
             count++; /* Kac kere dondurdugunu say, veya bul . */
             printf("line %d, column %d\n", current_lno, (int) (ptr_found - cur->text));
            
             /* Devami icin tekrar ariyoriuz ... */
             ptr_found = strstr(ptr_found + term_len, term);
        }
        
        cur = cur->next;
        current_lno++;
    }
    
    printf("%d match(es)\n", count);
}

/* KELIMELERI DOSYA UZERINDE KOPLE (ALL METINS) DEGISTIRME 
   Bu , cok hassas bir string kaydirma sistemidir! 
*/
int replace_all_text(const char *old_word, const char *new_word) {
    if (old_word == NULL || old_word[0] == '\0' || new_word == NULL) return 0;
    
    int old_len = strlen(old_word);
    int new_len = strlen(new_word);
    int replaced_count = 0;
    
    struct LineNode *cur = head;
    
    while (cur != NULL) {
        char *ptr_found = strstr(cur->text, old_word);
        
        while (ptr_found != NULL) {
            undo_snapshot(ACT_REPLACE_ALL); /* Yedekliyoruz!*/
            
            /* Strstring metnin "text" uzeride nerede ("pointer farki (offset)") nerede old. buluyoruz.*/
            int offset = (int) (ptr_found - cur->text);
            
            int i;
            if (new_len == old_len) {
                /* BASIT : Karakter uzunluklari esit = Uzerinde yazariz .*/
                for(i=0; i<new_len; i++){
                    cur->text[offset + i] = new_word[i];
                }
            }
            else if (new_len < old_len) {
                /* KISALDI : Uzunlugu daha buyuk (old), yenisi kucuk , o zaman dizide (string) boslugu "kapatmamiz" gerek ! */
                int difference = old_len - new_len;
                
                /* Once  degersiz eski harflei evelini  alip , geri sol kaydirin. */
                for(i = offset + old_len; i <= cur->len; i++) {
                     cur->text[i - difference] = cur->text[i];
                }
                
                /* Yeri hazirlanan o "sol", a (baslarina yeni harfleri d) eziyor. */
                for(i=0; i<new_len; i++){
                    cur->text[offset + i] = new_word[i];
                }
                cur->len -= difference;
            }
            else { /* new_len > old_len */
                /* UZADI : String metin boyutu  eski metinden uzayorsa yer "acilcaktir!" */
                int difference = new_len - old_len;
                
                ln_grow(cur, cur->len + difference); /* Aciga kapastamizi genislettil. Yeterli Hafizan!*/
                
                /* Eerki array "ptr_found" u referansini kaybedilebilir (realloc ! yapldgidan ptr adresleri giteyebilir !!!) */
                /* Cok onemli ! adresi yenile , (offset  var sayilarak yeniden referansini aliyoruz). !*/
                ptr_found = cur->text + offset;
                
                /* BOSLUK AC: Dongu Sag dan . \0 character ine "dahi" ( <= cur.len). kaydir . */
                for(i = cur->len; i >= offset + old_len; i--) {
                     cur->text[i + difference] = cur->text[i];
                }
                
                /* Eziyoruz Yeni Yazini .*/
                for(i=0; i<new_len; i++){
                    cur->text[offset + i] = new_word[i];
                }
                
                cur->len += difference;
            }
            
            replaced_count++;
            
            /* Devam et  , var ise !*/
            ptr_found = strstr(cur->text + offset + new_len, old_word);
        }
        
        cur = cur->next;
    }
    
    if (replaced_count > 0) is_dirty = 1;
    
    /* Satir kisaldiysa imlec satir sonundan one tasmasin. */
    if (cur_line_ptr != NULL && cur_col > cur_line_ptr->len) {
        cur_col = cur_line_ptr->len;
    }
    return replaced_count;
}

/* =============================================================
   UNDO AKSIYONU CAGIRMA 
   ============================================================= */

/* Listeye yollanmis undo yiginindaki veriyi geri sarmak.*/
void undo_action() {
    /* Hic yok (bos) yigin?*/
    if (undo_stack.top < 0) return;
    
    struct UndoAction a = undo_stack.items[undo_stack.top];
    undo_stack.top--; /* 1 ust seviye ye assagi "geri almis/pop" isaretleri ver */
    
    struct LineNode *n1 = ln_at(a.line_no); /* Hedef satiri getir */
    
    /* Eger satirlar birbirnine ayrilmissa(ACT_ENTER), eski line string geri kopyalanmali */
    if (a.type == ACT_ENTER) {
        if (n1 != NULL && n1->next != NULL) {
            ln_merge(n1, n1->next); /* Geri zimba(1 tek satira donustur)* */
            ln_del_node(n1->next);  /* Ikinci satiri sil !*/
        }
        
        /* Geri donulmis yedek string hafizada tutulans a geri  yap. (! ) */
        if (n1 != NULL && a.saved_text != NULL) {
            free(n1->text); 
            
            n1->len = strlen(a.saved_text);
            n1->cap = n1->len + 1;
            n1->text = (char *) malloc(n1->cap); 
            strcpy(n1->text, a.saved_text);
        }
        
    } else if (a.type == ACT_MERGE_LINE) {
        /* Birlestirmeden silen bir geri zmnsa: "satir 2 kere kopyanla ( 1 satir varken simdi altinda yeni satir yaranacak) !" */
        if (n1 != NULL && a.saved_text != NULL) {
            free(n1->text);
            n1->len = strlen(a.saved_text);
            n1->cap = n1->len + 1;
            n1->text = (char *) malloc(n1->cap);
            strcpy(n1->text, a.saved_text);
        }
        
        if (a.saved_text2 != NULL) {
            struct LineNode *new_second_node = ln_new();
            
            free(new_second_node->text); /* Gelen "64kb"  i bos verelim. */
            
            new_second_node->len = strlen(a.saved_text2);
            new_second_node->cap = new_second_node->len + 1;
            new_second_node->text = (char *) malloc(new_second_node->cap);
            strcpy(new_second_node->text, a.saved_text2);
            
            ln_ins_after(n1, new_second_node); /* O eski asil satirda oldugu yerde insa edilir./yapistirilir*/
        }
    } else {
        /* Karaktere islenen sadece string ezecek degisimleri !. (Isler genel olarak bu.)  */
        if (n1 != NULL && a.saved_text != NULL) {
            free(n1->text);
            n1->len = strlen(a.saved_text);
            n1->cap = n1->len + 1;
            n1->text = (char *) malloc(n1->cap);
            strcpy(n1->text, a.saved_text);
        }
    }

    /* Geri sarmislsek pointer/cursor lokasyonlarini  hafizadan gucelledik !.  */
    cur_line = a.cur_line;
    cur_col = a.cur_col;
    cur_line_ptr = ln_at(cur_line);
    
    /* Neolur ne olmaaz ptr null kontrol.*/
    if (cur_line_ptr == NULL) {
        cur_line = 1;
        cur_col = 0;
        cur_line_ptr = head;
    }
    
    /* Sutun asilirsa geri string sinirlara cek! */
    if (cur_col > cur_line_ptr->len) {
        cur_col = cur_line_ptr->len;
    }

    /* C pointerlarini malloc olan belleklerden ver !.*/
    if (a.saved_text != NULL) free(a.saved_text);
    if (a.saved_text2 != NULL) free(a.saved_text2);
    
    is_dirty = 1;
}

/* =============================================================
   DOSYA ISLEMI  (I/O YAZMA - OKUMA MANUPILATION)
   ============================================================= */

/* DOSYAYI BELGE / DISKE KAYDET.*/
void save_file(const char *path) {
    if (path == NULL) return;
    
    FILE *fp = fopen(path, "w"); /* Dosyayi  Write (w) "yazma modunde ac.*/
    if (fp == NULL) return;

    struct LineNode *cur = head;
    while (cur != NULL) {
        fputs(cur->text, fp); /* C standart i her dizini /satiri fputs() yapar .. */
        if (cur->next != NULL) {
            fputc('\n', fp); /*  \n i cikaricgi araya enter  */
        }
        cur = cur->next;
    }
    
    fclose(fp); /* Acilan baglantityi Kapat */
    is_dirty = 0; /* Artk document bozuk degil.*/
}

/* BELGEDEN AC VEYA DISKTEN OKU */
void load_file(const char *path) {
    if (path == NULL) return;
    
    FILE *fp = fopen(path, "r"); /* Read (r)  mod.*/
    if (fp == NULL) return;

    /* Tumuyle ilk once dosya (hafiz) islmeleri temizlenecek (Editor bos / first ) olicak. */
    struct LineNode *c = head;
    while (c != NULL) { 
        struct LineNode *next_node = c->next; 
        ln_free(c);  /* Siraya gore hafiza siliyor..*/
        c = next_node; 
    }
    head = NULL; 
    tail = NULL;
    total_lines = 0;
    
    char buffer[4096]; /* Cogu standart sarti line basian buffer i .*/
    int first_line = 1; 
    
    /* `fgets` i tum string "\n"(enter) geleseye degin yazi dondurecektir.*/
    while (fgets(buffer, sizeof(buffer), fp)) {
        int length = strlen(buffer);
        
        /* Satri onune bi islem (\n veya \r) varsa geride birakilmammasi icn temizlik !*/
        while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r')) {
            buffer[length - 1] = '\0';
            length--;
        }

        struct LineNode *newn = ln_new();
        ln_grow(newn, length); 
        
        int i;
        for (i = 0; i < length; i++) {
            newn->text[i] = buffer[i];
        }
        newn->text[length] = '\0';
        newn->len = length;
        
        ln_ins_after(tail, newn); /* Enek alttaki node 'a asagiyi bagliyoruz. !*/
        first_line = 0;
    }
    
    /* Eer kii dosya bir bos dosya (\0) okutuyor sa editor bos olamayaz  */
    if (first_line == 1) { 
        struct LineNode *newn = ln_new();
        ln_ins_after(NULL, newn);
    }
    
    fclose(fp); /* Diski kiritm.*/

    cur_line = 1;
    cur_col = 0;
    cur_line_ptr = head;
    
    is_dirty = 0; /* Dosya temz, daha edit yok!*/
}
